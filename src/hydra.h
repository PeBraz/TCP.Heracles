
#ifndef __TCP_HERACLES_H
#define __TCP_HERACLES_H 1

#include <linux/types.h>

struct hydra_subnet {
	// stores the upper 24 bits of the inet addresses of nodes in this group
	// I need to be careful, of type __be32 when shifting because of subnets
	__be32 inet_addr_24;
	struct hlist_node list_next;
	struct rb_root tree;
};

struct hydra_group {
	struct rb_node node;
	struct list_head heracles_list;

	size_t size;
	struct hydra_subnet *subnet;
	//GROUP INFO HERE
	u32 rtt;
	//indicate group initialization status
	int group_init;
	//for calculating 
	u32 ssthresh_total;
	/*number of connections in congestion avoidace */
	size_t in_ca_count;
};

struct heracles {
	struct list_head node;
	struct hydra_group *group;
	__be32 inet_addr;
        u32 rtt;
	//number of acks since init
	size_t acks;
	
	int in_ca;
	u32 old_ssthresh;
//for window increase after group leave
	//indicates if window is congestion limited, so it can increase
	int is_limited;
	/* other connections update this field to specify the amount it should increase*/
	u32 excess;
};


#define HYDRA_HASH_BITS 8

int hydra_cmp_with_interval(struct hydra_group *, struct heracles *);

static inline bool hydra_remains_in_group(struct heracles *heracles) 
{
	return !hydra_cmp_with_interval(heracles->group, heracles);
}

struct hydra_group *hydra_add_node(struct heracles *);
struct hydra_group *hydra_update(struct heracles *);
void hydra_remove_node(struct heracles*);
#endif
