#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <net/tcp.h>

#include "hydra.h"


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pedro Braz");
MODULE_DESCRIPTION("TCP Heracles");



//minimum number of acks for hydra to start 
#define MIN_ACKS 3 
// error while deciding on stalling an upper connection (cwnd stalls if CWND_ERROR + cwnd average > cwnd )
#define CWND_ERROR 10
#define HERACLES_SOCK_DEBUG(tp, her)\
	do {\
		printk(KERN_INFO "T: %d %u %d %d %d %d %d %d\n", her->id, her->inet_addr, tp->packets_out, tp->snd_cwnd, tp->snd_ssthresh,  her->rtt, tp->srtt_us, tp->mdev_us);\
		printk(KERN_INFO "H(group:%d,ak:%d,ca:%d,oss:%d,ocw:%d,e:%d), ", her->group?her->group->id:0, her->acks, her->in_ca, her->old_ssthresh, her->old_cwnd, her->events_ts);\
		if (her->group)\
			printk(KERN_INFO "G(size:%d,sst:%d,cwt:%d,acc:%d)", (int)her->group->size, her->group->ssthresh_total, her->group->cwnd_total, her->group->in_ca_count);\
	} while (0);\

void heracles_try_enter_ca(struct heracles *heracles, u32 ssthresh);
void heracles_try_leave_ca(struct heracles *heracles);
void heracles_add_event(struct heracles*, enum heracles_event);
bool heracles_is_event(struct heracles*);
void heracles_in_group_release(struct heracles*);


static int global_heracles_id = 1;

void tcp_heracles_init(struct sock *sk)
{
	struct heracles *heracles = inet_csk_ca(sk);
	*heracles = (struct heracles){
		.id=global_heracles_id++,
		.group=0,
		.inet_addr=sk->sk_daddr,
		.rtt=0,
		.acks=0,
		.in_ca=0,
		.old_ssthresh=0,
		.old_cwnd=0,
		.events_ts={0},
	};
}
EXPORT_SYMBOL_GPL(tcp_heracles_init);


//
//	Estimates the ssthresh from all the connections in the group that are in congestion avoidance.
//
static inline u32 heracles_ssthresh_estimate(struct heracles *heracles)
{
	return heracles->group->ssthresh_total / heracles->group->size;
}

//
// Estimates the cwnd from all the connections in the group
//
static inline u32 heracles_cwnd_estimate(struct heracles *heracles)
{
	return heracles->group->cwnd_total / heracles->group->size;
}

void tcp_heracles_release(struct sock *sk) 
{
	struct heracles *heracles = inet_csk_ca(sk);

	int id = heracles->id;


	if (heracles->group) {

		//	node leaves group without removing ssthresh information
		// 	signals event, so other connections can get the ssthresh that was released
		//printk(KERN_INFO "Node goes bye-bye %d\n", heracles->group->ssthresh_total);
	    heracles_in_group_release(heracles);
		hydra_remove_node(heracles);
	}
	tcp_heracles_init(sk);

	// ehhh awful way to do this
	global_heracles_id--;
	heracles->id = id;
}
EXPORT_SYMBOL_GPL(tcp_heracles_release);


void tcp_heracles_cwnd_event(struct sock *sk, enum tcp_ca_event event)
{
	if (event == CA_EVENT_CWND_RESTART ||
	    event == CA_EVENT_TX_START) {

		tcp_heracles_release(sk);
	}
}
EXPORT_SYMBOL_GPL(tcp_heracles_cwnd_event);



u32 tcp_reno2_slow_start(struct tcp_sock *tp, u32 acked)
{
	u32 cwnd = min(tp->snd_cwnd + acked, tp->snd_ssthresh);

	acked -= cwnd - tp->snd_cwnd;
	tp->snd_cwnd = min(cwnd, tp->snd_cwnd_clamp);

	return acked;
}
EXPORT_SYMBOL_GPL(tcp_reno2_slow_start);



void tcp_reno2_cong_avoid_ai(struct tcp_sock *tp, u32 w, u32 acked)
{	
	if (tp->snd_cwnd_cnt >= w) {
		tp->snd_cwnd_cnt = 0;
		tp->snd_cwnd++;
	}
 
	tp->snd_cwnd_cnt += acked;
	if (tp->snd_cwnd_cnt >= w) {
		u32 delta = tp->snd_cwnd_cnt / w;

		tp->snd_cwnd_cnt -= delta * w;
		tp->snd_cwnd += delta;
	}
	tp->snd_cwnd = min(tp->snd_cwnd, tp->snd_cwnd_clamp);
}
EXPORT_SYMBOL_GPL(tcp_reno2_cong_avoid_ai);


void heracles_update_group_ssthresh(struct heracles *heracles, u32 ssthresh) 
{	heracles->group->ssthresh_total -= heracles->old_ssthresh;
	heracles->group->ssthresh_total += ssthresh;
	heracles->old_ssthresh = ssthresh;
}
void heracles_update_group_cwnd(struct heracles *heracles, u32 cwnd) 
{
	heracles->group->cwnd_total -= heracles->old_cwnd;
	heracles->group->cwnd_total += cwnd;
	heracles->old_cwnd = cwnd;
}

//
// heracles_add_event - notifies the group of an event (JOIN, LEAVE, LOSS) 
// 		and the cwnd/ssthresh state at the start of the event. 
//			Replaces previous events of the same type.
//
void heracles_add_event(struct heracles *heracles, enum heracles_event event) 
{
	heracles->group->events[event].ts++;
	heracles->group->events[event].cwnd = heracles_cwnd_estimate(heracles);
	heracles->group->events[event].ssthresh = heracles_ssthresh_estimate(heracles);

	heracles->events_ts[event] = heracles->group->events[event].ts;
	printk(KERN_INFO "NEW event (her.id:%d): %s -> %d\n", heracles->id, (char*[]){"JOIN","LOSS","LEAVE"}[event], heracles->group->events[event].ts);
}


//
// A new connection joins a group:
//	1) it updates its events to equal the group's events
//  2) update group ssthresh/cwnd
//  3) signal JOIN event
//
void heracles_join(struct sock *sk)
{
	struct heracles *heracles = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);

	hydra_add_node(heracles);

	heracles->events_ts[HER_JOIN] = heracles->group->events[HER_JOIN].ts;
	heracles->events_ts[HER_LEAVE] = heracles->group->events[HER_LEAVE].ts;
	heracles->events_ts[HER_LOSS] = heracles->group->events[HER_LOSS].ts;

	if (!tcp_in_initial_slowstart(tp)) 
		heracles_update_group_ssthresh(heracles, min(tp->snd_cwnd, tp->snd_ssthresh));

	heracles_update_group_cwnd(heracles, min(tp->snd_cwnd, tp->snd_ssthresh));

	heracles_add_event(heracles, HER_JOIN);
}



//
// 		This is a basic implementation when polling for events. 
//	A lot of events can be enqueued, but it becomes harder to deal with them.
//	We only deal with the most recent, highest priority event, all others disappear and aren't dealt with 
//	(the differences between them aren't many).
//
//		For priority we assume JOIN must be taken care as soon as possible (it should also be the rarest).
//	CWND/SSTHRESH drop during JOIN/LOSS and go up during LEAVEs.
//
//		Doing it like this, shouldn't create any meaningful difference.
//
enum heracles_event heracles_poll_event(struct heracles *heracles) 
{

	int event;
	for (event=0; event < NUMBER_HERACLES_EVENTS; event++) {
		if (heracles->group->events[event].ts > heracles->events_ts[event]) {
			heracles->events_ts[HER_JOIN] = heracles->group->events[HER_JOIN].ts;
			heracles->events_ts[HER_LEAVE] = heracles->group->events[HER_LEAVE].ts;
			heracles->events_ts[HER_LOSS] = heracles->group->events[HER_LOSS].ts;
			printk(KERN_INFO "POLL %p: %s -> %d\n", heracles, (char*[]){"JOIN","LOSS","LEAVE"}[event], heracles->group->events[event].ts);
			return (enum heracles_event)event;
		}
	}
	return HER_NULL;
}



// Before a node is released from a group, it must first share cwnd/ssthersh information
// with the rest of the group
// If the node is alone in the group do nothing 
void heracles_in_group_release(struct heracles *heracles) 
{
		//DBG();
	if (heracles->group->size > 1) {
		heracles->group->size--; // size is used to calculate cwnd/ssthresh for event
    	heracles_add_event(heracles, HER_LEAVE);
    	heracles->group->size++;
	}
	
	heracles->group->ssthresh_total -= heracles->old_ssthresh;
	heracles->group->cwnd_total -= heracles->old_cwnd;

}

void heracles_event_handling(struct sock *sk)
{
		DBG();
	struct tcp_sock *tp = tcp_sk(sk);
	struct heracles *heracles = inet_csk_ca(sk);

	switch(heracles_poll_event(heracles)) {
		case HER_NULL: return;

		/* Loss estimate new ssthresh and decrease*/
		case HER_LOSS:
			tp->snd_ssthresh = heracles->group->events[HER_LOSS].ssthresh;
			tp->snd_cwnd = heracles->group->events[HER_LOSS].ssthresh;
			return;
		/* Another connection joined group, update ssthresh and start from it (drop down)*/
		case HER_JOIN:
			tp->snd_ssthresh = heracles->group->events[HER_JOIN].ssthresh;
			tp->snd_cwnd = heracles->group->events[HER_JOIN].ssthresh;
			return;

		/* A connection left update ssthresh  (increase), cwnd will increase in slow start*/
		case HER_LEAVE:
			/* why do i want a different cwnd on leave??
			tp->snd_cwnd = heracles_ssthresh_estimate(heracles); // slow start instead? only up to ssthresh
			heracles->old_cwnd = tp->snd_cwnd;	// someone has to clean this mess
			*/
			tp->snd_ssthresh = heracles->group->events[HER_LEAVE].cwnd; // take estimate from cwnd instead of ssthresh
			//heracles->old_ssthresh = heracles->group->events[HER_LEAVE].cwnd;
			return;

		default:
			BUG();
			return;
	}
}


void heracles_group_skip(struct sock*sk) {

	struct tcp_sock *tp = tcp_sk(sk);
	struct heracles *heracles = inet_csk_ca(sk);

	tp->snd_ssthresh = heracles_ssthresh_estimate(heracles);
}

// heracles_ss_skip: when cwnd < ssthresh, tries to insert connection in a group

bool heracles_ss_skip(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct heracles *heracles = inet_csk_ca(sk);

	if (!heracles->group && heracles->acks >= MIN_ACKS) {
	//if (heracles_ssthresh_estimate(heracles) > tp->snd_ssthresh && heracles->acks >= MIN_ACKS) {
		heracles_join(sk);
		// Slow Start Skip Conditions
		//	1. connection is in a group (atleast 3 acks for RTT estimation)
		//	2. alteast another connection is in Congestion Avoidance
		//	3. Jumping slow start will increase the connection's congestion window


		//experiment instead of the following:
		//if (heracles->group->in_ca_count > 0 && heracles_ssthresh_estimate(heracles) > tp->snd_cwnd) {
		if (heracles_ssthresh_estimate(heracles) > tp->snd_cwnd) {
			//printk(KERN_INFO "SKIPPING SS - totalss:%d ss:%d cwnd:%d group_size:%d in_ca_count:%d \n", heracles->group->ssthresh_total, tp->snd_ssthresh, tp->snd_cwnd, heracles->group->size, heracles->group->in_ca_count);
			
			tp->snd_ssthresh = max(heracles_ssthresh_estimate(heracles), 2U);
			
			//heracles_group_skip(sk);
			return true;
		}
	}
	return false;
}


void heracles_ca(struct sock *sk) 
{
	struct heracles *heracles = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);

	// Connection tries to join group during Congestion avoidance
	if (!heracles->group && heracles->acks >= MIN_ACKS) {
		// Check if joined a group successfully
		heracles_join(sk);
		tp->snd_ssthresh = max(heracles_ssthresh_estimate(heracles), 2U);
		//heracles_group_skip(sk);
	}
	
	if (!heracles->group)
		return;

	heracles_event_handling(sk);

	heracles_update_group_cwnd(heracles, tp->snd_cwnd);
	heracles_update_group_ssthresh(heracles, tp->snd_ssthresh);
}



void tcp_heracles_cong_avoid(struct sock *sk, u32 ack, u32 acked)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct heracles *heracles = inet_csk_ca(sk);

	HERACLES_SOCK_DEBUG(tp, heracles);


	if (!tcp_is_cwnd_limited(sk))
		return;

	/*
	if (heracles->group && tp->snd_cwnd < tp->snd_ssthresh) {
		heracles_event_handling(sk);
		tcp_reno2_cong_avoid_ai(tp, tp->snd_cwnd, acked);
		heracles_update_group_cwnd(heracles, tp->snd_cwnd);
		heracles_update_group_ssthresh(heracles, tp->snd_ssthresh);
		return;
	}*/
			

	/* In "safe" area, increase. */
	if (tp->snd_cwnd < tp->snd_ssthresh) {
		/* There are a few cases where a connection in slow start is in a group, but doesn't skip*/
		if (heracles->group) 
			heracles_event_handling(sk);
		/* tries to skip slow start */
		if (!heracles_ss_skip(sk)) {
			acked = tcp_reno2_slow_start(tp, acked);
			if (!acked)
				return;
		}
		else return;
	}

	/* In dangerous area, increase slowly. */
	tcp_reno2_cong_avoid_ai(tp, tp->snd_cwnd, acked);

	heracles_ca(sk);

}
EXPORT_SYMBOL_GPL(tcp_heracles_cong_avoid);


void tcp_heracles_pkts_acked(struct sock *sk, u32 acked, s32 rtt)
{
	struct heracles *heracles = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	heracles->acks += acked;
	heracles->rtt = rtt;

	if (!heracles->group) return;


	if (hydra_remains_in_group(heracles)) return;

	// Before leaving
	// leave event, send LEAVE event, decrement total_cwnd/total_ssthresh 
	heracles_in_group_release(heracles);
	// restart heracles information
	heracles->old_ssthresh = 0;
	heracles->old_cwnd = 0;
	heracles->acks = acked; 
	heracles->rtt = rtt;

	//update, changing group
	hydra_update(heracles);

	//In new group
	heracles->events_ts[HER_JOIN] = heracles->group->events[HER_JOIN].ts;
	heracles->events_ts[HER_LEAVE] = heracles->group->events[HER_LEAVE].ts;
	heracles->events_ts[HER_LOSS] = heracles->group->events[HER_LOSS].ts;


	if (!tcp_in_initial_slowstart(tp)) 
		heracles_update_group_ssthresh(heracles, min(tp->snd_cwnd, tp->snd_ssthresh));


	heracles_update_group_cwnd(heracles, min(tp->snd_cwnd, tp->snd_ssthresh));

	heracles_add_event(heracles, HER_JOIN);
}
EXPORT_SYMBOL_GPL(tcp_heracles_pkts_acked);

u32 tcp_heracles_ssthresh(struct sock *sk)
{
	const struct tcp_sock *tp = tcp_sk(sk);
	struct heracles *h = inet_csk_ca(sk);

	// do i need to check: "(!tcp_in_initial_slowstart(tp)"" ???
	if (h->group) {

		u32 new_ssthresh = max(tp->snd_cwnd >> 1U, 2U);
		heracles_update_group_cwnd(h, new_ssthresh); // this will become sstresh
		heracles_update_group_ssthresh(h, heracles_cwnd_estimate(h));
		heracles_add_event(h, HER_LOSS);
		return max(heracles_ssthresh_estimate(h), 2U);
	}
	return max(tp->snd_cwnd >> 1U, 2U); 
}

EXPORT_SYMBOL(tcp_heracles_ssthresh);

struct tcp_congestion_ops tcp_heracles = {
	.init		= tcp_heracles_init,
	.flags		= TCP_CONG_NON_RESTRICTED,
	.name		= "heracles",
	.owner		= THIS_MODULE,
	.ssthresh	= tcp_heracles_ssthresh,
	.cong_avoid	= tcp_heracles_cong_avoid,
	.pkts_acked	= tcp_heracles_pkts_acked,
	.cwnd_event	= tcp_heracles_cwnd_event,
	.release	= tcp_heracles_release,
};


static int __init heracles_init(void)
{
	//BUILD_BUG_ON(sizeof(struct heracles) > ICSK_CA_PRIV_SIZE);
	printk(KERN_INFO "Initializing our hero and savior: HERACLES!!\n");
	tcp_register_congestion_control(&tcp_heracles);
	return 0;
}



static void __exit heracles_exit(void)
{
	tcp_unregister_congestion_control(&tcp_heracles);
	printk(KERN_INFO "Heracles cleanup success.\n");
}


module_init(heracles_init);
module_exit(heracles_exit);
