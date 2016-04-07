
#include <linux/hashtable.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/rbtree.h>

#include "tcp_heracles.h"
#include "hydra.h"

#define HYDRA_KEY_MASK 0xF0




struct hydra_subnet {
	// stores the upper 24 bits of the inet addresses of nodes in this group
	u32 inet_addr_24;
	struct hlist_node list_next;
	struct rb_root *tree;
};

struct hydra_group {
	struct rb_node node;

	size_t size;
	struct hydra_subnet *subnet;
	//GROUP INFO HERE
	u32 rtt;
	//indicate group initialization status
	int group_init;
};


struct heracles {
	struct hydra_group *group;
	u32 inet_addr;
        int size;
        u32 rtt;
};

#define HYDRA_HASH_BITS 8
static DEFINE_HASHTABLE(hydra, HYDRA_HASH_BITS);

struct hydra_group *hydra_add_node(struct heracles *node);
int hydra_cmp_with_interval(struct hydra_group *group, struct heracles *node);
struct hydra_subnet *hydra_remove_group(struct heracles *heracles, int clear_subnet);
void hydra_remove_node(struct heracles* heracles);
struct hydra_group * hydra_insert_in_subnet(struct hydra_subnet *sub, struct heracles *heracles);
void hydra_insert_in_group(struct hydra_group *group, struct heracles *node);
struct hydra_group *hydra_init_group(struct hydra_subnet *subnet, struct heracles * heracles);
struct hydra_subnet *hydra_init_subnet(struct heracles *heracles);
struct hydra_group *hydra_update(struct heracles *heracles);
void hydra_update_rcu(struct heracles * node);
struct hydra_group *hydra_search(struct hydra_subnet *sub, struct heracles *heracles);
void hydra_remove_subnet(struct hydra_subnet *subnet);

struct hydra_group *hydra_add_node(struct heracles *node)
{
	//are locks needed?

	int bkt;
	struct hydra_subnet *sub_pt;

	//check if subnet already exists (must have atleast 1 group)
	hash_for_each(hydra, bkt, sub_pt, list_next) {

		BUG_ON(!sub_pt->tree->rb_node);

		if (sub_pt->inet_addr_24 == node->inet_addr >> 8)
			return hydra_insert_in_subnet(sub_pt, node);
	}

	//create subnet and group 
	struct hydra_subnet * new_subnet = hydra_init_subnet(node);
	hash_add(hydra, &new_subnet->list_next, node->inet_addr & HYDRA_KEY_MASK);
	
	struct rb_node * root_node = new_subnet->tree->rb_node;
	return container_of(root_node, struct hydra_group, node);
}



// should take into account rtt and variance of node and group
// (+ maybe amount of samples)
// (should be made easy to tune)
int hydra_cmp_with_interval(struct hydra_group *group, struct heracles *node)
{
	return node->rtt > group->rtt * 0.9 && node->rtt < group->rtt * 1.1;
}


//
//	@clear_subnet	indicate if subnet should be cleared from the table if the tree becomes empty

struct hydra_subnet *hydra_remove_group(struct heracles *heracles, int clear_subnet)
{
	BUG_ON(heracles || heracles->size > 1);

	struct hydra_subnet * subnet = heracles->group->subnet;

	rb_erase(&heracles->group->node, subnet->tree);
	kfree(heracles->group);
	heracles->group = NULL;

	if (clear_subnet && !subnet->tree->rb_node) {// if subnet has a empty tree with no groups inside
		hydra_remove_subnet(subnet);
		return NULL;
	}
	return subnet;
}


//
// Removes subnet when no group is found inside the tree
//
void hydra_remove_subnet(struct hydra_subnet *subnet)
{
	//  -- can rcu lock
	hash_del(&subnet->list_next);
}


void hydra_remove_node(struct heracles* heracles)
{
	if (heracles->group->size == 1)
		hydra_remove_group(heracles, 1);
	else {
		--heracles->group->size;
		//should i do more stuff here???
	}
}

/*
 * Traverses the tree until it finds the group where to insert the new node (or create it)
 */
struct hydra_group * hydra_insert_in_subnet(struct hydra_subnet *sub, struct heracles *heracles)
{

	struct rb_node **node = &(heracles->group->subnet->tree->rb_node), *parent = NULL;

	while (*node) {
		struct hydra_group * group = container_of(*node, struct hydra_group, node); //check this one is node, the other is heracles
		int res = hydra_cmp_with_interval(group, heracles);

		parent = *node;
		if (res < 0) {
			node = &((*node)->rb_left);

		} else if (res > 0) {
			node = &((*node)->rb_right);
		} else {
			hydra_insert_in_group(group, heracles);
			return group;
		}
	}


	struct hydra_group *group = hydra_init_group(sub, heracles);

	rb_link_node(&group->node, parent, node);
	rb_insert_color(&group->node, group->subnet->tree);
	return group;
}

//updates the old group
//inserting the new node inside the group
void hydra_insert_in_group(struct hydra_group *group, struct heracles *node) {
	 group->size += 1;
	 //should do + stuff
}

//initializes a new group,
struct hydra_group *hydra_init_group(struct hydra_subnet *subnet, struct heracles * heracles) 
{
	struct hydra_group *group = kmalloc(sizeof(struct hydra_group), GFP_KERNEL);
	BUG_ON(!group);
	*group = (struct hydra_group) {
		.subnet = subnet,
		.size = 1,
		.group_init = 0
	};
	return group;
}

//create a new hydra subnet for the specified node
//needs to add the subnet to the hash table list
struct hydra_subnet *hydra_init_subnet(struct heracles *heracles) 
{
	struct hydra_subnet *sub = kmalloc(sizeof(struct hydra_subnet), GFP_KERNEL);
	BUG_ON(!sub);
	*sub = (struct hydra_subnet) {
		.inet_addr_24 = heracles->inet_addr >> 8,
		.tree = &RB_ROOT
	};
	hydra_insert_in_subnet(sub, heracles);
	return sub;
}


// TODO: enable RCU locking, while inserting/traversing tree

void hydra_update_rcu(struct heracles * node) {
	//lockrcu
	hydra_update(node);
	//unlockrcu
}


struct hydra_group *hydra_update(struct heracles *heracles)
{

	 //  --  lock tree  --

	if (heracles->group->size == 1) {
		hydra_remove_group(heracles, 0);
		return hydra_add_node(heracles);
	}
	return hydra_insert_in_subnet(heracles->group->subnet, heracles);
	/*
	struct rb_node **node = &(heracles->group->subnet->tree->rb_node), *parent = NULL;

	while (*node) {
		struct hydra_group * group = container_of(node, struct hydra_group, node);
		int res = hydra_cmp_with_interval(group, heracles);

		parent = *node;
		if (res < 0) {
			node = &((*node)->rb_left);

		} else if (res > 0) {
			node = &((*node)->rb_right);
		} else {
			hydra_insert_in_group(group, heracles);
			return group;
		}
	}

	//create a new group
	group = hydra_init_group(subnet, heracles);

	rb_link_node(&group->node, parent, node);
	rb_insert_color(&group->node, group->subnet->tree);
	return group;*/

}


struct hydra_group *hydra_search(struct hydra_subnet *sub, struct heracles *heracles)
{
	struct rb_node *node = sub->tree->rb_node;

	while (node) {
		struct hydra_group * group = container_of(node, struct hydra_group, node);
		int res = hydra_cmp_with_interval(group, heracles);

		if (res== 0) {
			if (!node->rb_left)  return NULL;
			node = node->rb_left;

		} else if (res > 0) {
			if (!node->rb_right) return NULL;
			node = node->rb_right;

		} else {
			hydra_insert_in_group(group, heracles);
			return group;
		}

	}
	return NULL;
}




