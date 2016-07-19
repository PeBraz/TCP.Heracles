
#ifndef __TCP_HERACLES_H
#define __TCP_HERACLES_H 1

#include <linux/types.h>

#define DBG() printk(KERN_INFO "[heracles]: %s\n", __FUNCTION__);

#define NUMBER_HERACLES_EVENTS 3

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
	u32 cwnd_total;
	/*number of connections in congestion avoidace */
	size_t in_ca_count;
	int events_ts[NUMBER_HERACLES_EVENTS];	//use to signal sshtresh events
};


enum heracles_event {
	HER_JOIN = 0x0,	// connection jumped up to estimated ssthresh
	HER_LOSS = 0x1,	// connection loss 
	HER_LEAVE = 0x2,// connection left group
	HER_NULL,
};



struct heracles {
	struct list_head node;
	struct hydra_group *group;
	__be32 inet_addr;
	int id;
    u32 rtt;
    size_t acks; //number of acks since init
	
	int in_ca;
	u32 old_ssthresh;
	u32 old_cwnd;

	//int is_limited;
	//u32 excess;

	int events_ts[NUMBER_HERACLES_EVENTS]; //event counter for each event
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
