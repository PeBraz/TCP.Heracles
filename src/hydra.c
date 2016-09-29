
#include <linux/hashtable.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/rbtree.h>
#include <linux/spinlock.h>

#include "hydra.h"


#define HYDRA_KEY_MASK 0x00FF0000
#define HYDRA_SUBNET_MASK 0x00FFFFFF

#define HYDRA_HASH_BITS 8
static DEFINE_HASHTABLE(hydra, HYDRA_HASH_BITS);
static int heracles_counter = 1;
static DEFINE_SPINLOCK(hydra_lock);

struct hydra_group *__hydra_add_node(struct heracles *heracles);
int hydra_cmp_with_interval(struct hydra_group *group, struct heracles *heracles);
struct hydra_subnet *hydra_remove_group(struct heracles *heracles, int clear_subnet);
void hydra_remove_node(struct heracles* heracles);
void __hydra_remove_node(struct heracles* heracles);
struct hydra_group * hydra_insert_in_subnet(struct hydra_subnet *sub, struct heracles *heracles);
void hydra_insert_in_group(struct hydra_group *group, struct heracles *node);
struct hydra_group *hydra_init_group(struct hydra_subnet *subnet, struct heracles * heracles);
struct hydra_subnet *hydra_init_subnet(struct heracles *heracles);
struct hydra_group *__hydra_update(struct heracles *heracles);
struct hydra_group *hydra_search(struct hydra_subnet *sub, struct heracles *heracles);
void hydra_remove_subnet(struct hydra_subnet *subnet);



//whenever a new group is created, give it a identifier
int global_group_id = 1;


struct hydra_group *hydra_add_node(struct heracles *heracles)
{
	spin_lock(&hydra_lock);
	if (heracles->group) return heracles->group;
	struct hydra_group *g = __hydra_add_node(heracles);
	spin_unlock(&hydra_lock);
	return g;

}

struct hydra_group *__hydra_add_node(struct heracles *heracles)
{
	struct hydra_subnet *sub_pt;
	BUG_ON(!heracles);
	if (!heracles->id)
		heracles->id = heracles_counter++;

	hash_for_each_possible(hydra, sub_pt, list_next, (heracles->inet_addr & HYDRA_KEY_MASK)) {

		if (sub_pt->inet_addr_24 == (heracles->inet_addr & HYDRA_SUBNET_MASK))
			return hydra_insert_in_subnet(sub_pt, heracles);
		
	}
	struct hydra_subnet * new_subnet = hydra_init_subnet(heracles);
	hash_add(hydra, &new_subnet->list_next, heracles->inet_addr & HYDRA_KEY_MASK);
	
	struct rb_node * root_node = new_subnet->tree.rb_node;
	return container_of(root_node, struct hydra_group, node);
}



// should take into account rtt and variance of node and group
// (+ maybe amount of samples)
// (should be made easy to tune)
int hydra_cmp_with_interval(struct hydra_group *group, struct heracles *node)
{
	//return node->rtt > group->rtt * 0.9 && node->rtt < group->rtt * 1.1;
	if ((node->rtt * 10) < (group->rtt * 5)) return -1;
	if ((node->rtt * 10) > (group->rtt * 15)) return 1;
	return 0;
}


//
//	@clear_subnet	indicate if subnet should be cleared from the table if the tree becomes empty

struct hydra_subnet *hydra_remove_group(struct heracles *heracles, int clear_subnet)
{
	BUG_ON(!heracles || heracles->group->size != 1);

	struct hydra_subnet * subnet = heracles->group->subnet;

	rb_erase(&heracles->group->node, &subnet->tree);
	kfree(heracles->group);
	heracles->group = NULL;
	

	if (clear_subnet && !subnet->tree.rb_node) {// if subnet has a empty tree with no groups inside
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
	hash_del(&subnet->list_next);
}

void hydra_remove_node(struct heracles *heracles)
{
	spin_lock(&hydra_lock);
	__hydra_remove_node(heracles);
	spin_unlock(&hydra_lock);
}


void __hydra_remove_node(struct heracles* heracles)
{
	list_del(&heracles->node);
	if (heracles->group->size == 1){
		//printk(KERN_INFO "REMOVED GROUP h:%d; g:%d;\n", heracles->id, heracles->group->id);
		hydra_remove_group(heracles, 1);
	} else {
		//printk(KERN_INFO "LEFT GROUP h:%d; g:%d size:%d;\n", heracles->id, heracles->group->id, heracles->group->size-1);
		--heracles->group->size;
		heracles->group = NULL;
		//should i do more stuff here???
	}
}

/*
 * Traverses the tree until it finds the group where to insert the new node (or create it)
 */
struct hydra_group * hydra_insert_in_subnet(struct hydra_subnet *sub, struct heracles *heracles)
{
	struct rb_node **node = &(sub->tree.rb_node), *parent = NULL;

	while (*node) {
		struct hydra_group * group = container_of(*node, struct hydra_group, node); //check this one is node, the other is heracles
		int res = hydra_cmp_with_interval(group, heracles);

		parent = *node;
		if (res < 0) {
			node = &((*node)->rb_left);

		} else if (res > 0) {
			node = &((*node)->rb_right);
		} else {
			//printk(KERN_INFO "FOUND PREEXISTING GROUP h:%d; g:%d;\n",heracles->id, group->id);
			hydra_insert_in_group(group, heracles);
			return group;
		}
	}


	struct hydra_group *group = hydra_init_group(sub, heracles);
	heracles->group = group;


	//using group->id and global group_id for facilitating dbugging
	heracles->group->id = global_group_id;
	global_group_id += 1;
	///////////////////////////////////////////////////////////////


	//printk(KERN_INFO "CREATED GROUP h:%d; g:%d;\n", heracles->id, group->id);

	rb_link_node(&group->node, parent, node);
	rb_insert_color(&group->node, &group->subnet->tree);
	return group;
}


//updates the old group
//inserting the new node inside the group
void hydra_insert_in_group(struct hydra_group *group, struct heracles *heracles)
{
	group->size += 1;
	group->rtt = heracles->rtt; //maybe do an average?
	heracles->group = group;
	list_add(&heracles->node, &group->heracles_list);
}

//initializes a new group,
struct hydra_group *hydra_init_group(struct hydra_subnet *subnet, struct heracles * heracles) 
{
	struct hydra_group *group = kmalloc(sizeof(struct hydra_group), GFP_NOWAIT);
	BUG_ON(!group);
	*group = (struct hydra_group) {
		.subnet = subnet,
		.size = 1,
		.group_init = 0,
		.ssthresh_total = 0,
		.cwnd_total = 0,
		.in_ca_count = 0,
		.rtt = heracles->rtt, 	//this could be updated
		.heracles_list = LIST_HEAD_INIT(group->heracles_list),
	};

	int i;
	for (i=0; i < NUMBER_HERACLES_EVENTS; i++) {
		group->events[i].ts = 0;
		group->events[i].cwnd = 0;
		group->events[i].ssthresh = 0;
	}

	list_add(&heracles->node, &group->heracles_list);
	return group;
}

//create a new hydra subnet for the specified node
//needs to add the subnet to the hash table list
struct hydra_subnet *hydra_init_subnet(struct heracles *heracles) 
{
	struct hydra_subnet *sub = kmalloc(sizeof(struct hydra_subnet), GFP_NOWAIT);
	BUG_ON(!sub);
	*sub = (struct hydra_subnet) {
		.inet_addr_24 = heracles->inet_addr & HYDRA_SUBNET_MASK,
		.tree = RB_ROOT
	};


	hydra_insert_in_subnet(sub, heracles);
	return sub;
}

struct hydra_group *hydra_update(struct heracles *heracles)
{
	if (hydra_remains_in_group(heracles)) return heracles->group; //no need to update, will stay in same group

	spin_lock(&hydra_lock);
	struct hydra_group *g = __hydra_update(heracles);
	spin_unlock(&hydra_lock);
	return g;
}

/*
 * Returns NULL if stayed in same group
 *
 *
 */
struct hydra_group *__hydra_update(struct heracles *heracles)
{

	BUG_ON(!heracles);
	BUG_ON(!heracles->group);

	struct hydra_group *group = heracles->group; 	
	if (heracles->group && heracles->group->size == 1) {
		
		__hydra_remove_node(heracles);	//remove the node from the group before changing groups
		//hydra_remove_group(heracles, 0); // the 0 is so that the subnet wasnt cleared, as a performance thing, but fuck it
		return __hydra_add_node(heracles);
	}
	__hydra_remove_node(heracles);
	return hydra_insert_in_subnet(group->subnet, heracles);
}


struct hydra_group *hydra_search(struct hydra_subnet *sub, struct heracles *heracles)
{
	struct rb_node *node = sub->tree.rb_node;

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




