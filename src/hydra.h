
#ifndef __TCP_HERACLES_H
#define __TCP_HERACLES_H 1


struct hydra_subnet {
	// stores the upper 24 bits of the inet addresses of nodes in this group
	u32 inet_addr_24;
	struct hlist_node *node;
	struct rb_root *tree;
	struct hlist_head *table;
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

#define HASH_BITS 8
#define HYDRA_DECLARE(name) HASHTABLE_DECLARE(name, HASH_BITS)
#define hydra_init(hydra) hash_init(hydra)

static inline void hydra_remove_subnet(struct hydra_subnet *sub)
{
	hash_del(sub->node);
}

struct hydra_group *hydra_add_node(struct hlist_head *, struct heracles *);
struct hydra_group *hydra_update(struct heracles *);
void hydra_remove_node(struct heracles*);

#endif