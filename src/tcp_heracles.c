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
	printk(KERN_INFO "%p %u %d %d %d %d %d %d %d\n", her, her->inet_addr, tp->packets_out, tp->snd_cwnd, tp->snd_ssthresh, tp->mss_cache, her->rtt, tp->srtt_us, tp->mdev_us);

void heracles_try_enter_ca(struct heracles *heracles, u32 ssthresh);
void heracles_try_leave_ca(struct heracles *heracles);
void heracles_add_event(struct heracles*, enum heracles_event);
bool heracles_is_event(struct heracles*);
void heracles_in_group_release(struct heracles*);

void tcp_heracles_init(struct sock *sk)
{
	struct heracles *heracles = inet_csk_ca(sk);
	*heracles = (struct heracles){
		.id=0,
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


//static enum heracles_event[]  ;//????



void tcp_heracles_release(struct sock *sk) 
{
	struct heracles *heracles = inet_csk_ca(sk);
	if (heracles->group) {

		//	node leaves group without removing ssthresh information
		// 	signals event, so other connections can get the ssthresh that was released
		//printk(KERN_INFO "Node goes bye-bye %d\n", heracles->group->ssthresh_total);
	    heracles_in_group_release(heracles);
	    heracles_add_event(heracles, HER_LEAVE);
		hydra_remove_node(heracles);
	}
	tcp_heracles_init(sk);
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
{
	heracles->group->ssthresh_total -= heracles->old_ssthresh;
	heracles->group->ssthresh_total += ssthresh;
	heracles->old_ssthresh = ssthresh;
}
void heracles_update_group_cwnd(struct heracles *heracles, u32 cwnd) 
{
	heracles->group->cwnd_total -= heracles->old_cwnd;
	heracles->group->cwnd_total += cwnd;
	heracles->old_cwnd = cwnd;
}

void heracles_add_event(struct heracles *heracles, enum heracles_event event) 
{
	heracles->group->events_ts[event]++;
	heracles->events_ts[event] = heracles->group->events_ts[event];
	printk(KERN_INFO "NEW event: %s -> %d\n", (char*[]){"JOIN","LOSS","LEAVE"}[event], heracles->group->events_ts[event]);
}


void heracles_join() {}

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
		if (heracles->group->events_ts[event] > heracles->events_ts[event]) {
			heracles->events_ts[HER_JOIN] = heracles->group->events_ts[HER_JOIN];
			heracles->events_ts[HER_LEAVE] = heracles->group->events_ts[HER_LEAVE];
			heracles->events_ts[HER_LOSS] = heracles->group->events_ts[HER_LOSS];
			printk(KERN_INFO "POLL: %s -> %d\n", (char*[]){"JOIN","LOSS","LEAVE"}[event], heracles->group->events_ts[event]);
			return (enum heracles_event)event;
		}
	}
	return HER_NULL;
}


/*
 * whenever a connection is updating congestion window, it checks if it was previously in CA
 * If it was, then it updates the ssthresh for the group
 * If not it will enter CA and update the group
 *
 */
void heracles_try_enter_ca(struct heracles *heracles, u32 ssthresh)
{
	if (heracles->in_ca) {
		//printk(KERN_INFO "(IN CA) Updading ssthresh: %p - %u\n", heracles, heracles->group->ssthresh_total);
		heracles->group->ssthresh_total -= heracles->old_ssthresh;
		heracles->group->ssthresh_total += ssthresh;
		heracles->old_ssthresh = ssthresh;
		return;
	}

	heracles->in_ca = 1;
	heracles->group->in_ca_count++;
	heracles->group->ssthresh_total += ssthresh;
	heracles->old_ssthresh = ssthresh;
}

void heracles_try_leave_ca(struct heracles *heracles) 
{
	if (!heracles->in_ca) return;

	heracles->in_ca = 0;
	heracles->group->in_ca_count--; 
	heracles->group->ssthresh_total -= heracles->old_ssthresh;
	heracles->old_ssthresh = 0;
}


void heracles_in_group_release(struct heracles *heracles) 
{
	if (heracles->in_ca) 
		heracles->group->in_ca_count--;
}

//
//	Estimates the ssthresh from all the connections in the group that are in congestion avoidance.
//
static inline u32 heracles_ssthresh_estimate(struct heracles *heracles)
{
	printk(KERN_INFO "SS_ESTIMATE: %d %d\n", heracles->group->ssthresh_total, heracles->group->size);
	return heracles->group->ssthresh_total / heracles->group->size;
}

//
// Estimates the cwnd from all the connections in the group
//
static inline u32 heracles_cwnd_estimate(struct heracles *heracles)
{
	printk(KERN_INFO "CWND_ESTIMATE: %d %d\n", heracles->group->cwnd_total, heracles->group->size);
	return heracles->group->cwnd_total / heracles->group->size;
}

void heracles_event_handling(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct heracles *heracles = inet_csk_ca(sk);

	switch(heracles_poll_event(heracles)) {
		case HER_NULL: return;

		/* Loss estimate new ssthresh and decrease*/
		case HER_LOSS:
		/* Another connection joined group, update ssthresh and start from it (drop down)*/
		case HER_JOIN:
			tp->snd_ssthresh = heracles_ssthresh_estimate(heracles);
			tp->snd_cwnd = tp->snd_ssthresh;
			return;

		/* A connection left update ssthresh  (increase), cwnd will increase in slow start*/
		case HER_LEAVE:
			tp->snd_cwnd = heracles_ssthresh_estimate(heracles); // slow start instead? only up to ssthresh
			tp->snd_ssthresh = heracles_cwnd_estimate(heracles); // take estimate from cwnd instead of ssthresh
			return;

		default:
			BUG();
			return;

	}
}

// When a connection enters CA from SSS:
//	1. add its own ssthresh to the group (heracles_try_enter_ca)
// 	2. gets is own estimate for ssthresh (c1 + c2 + ... + cn) / N
//	3. Updates its CWND
//	4. Updates group ssthresh
//	5. Signals event to group
void heracles_group_skip(struct sock*sk) {
	struct tcp_sock *tp = tcp_sk(sk);
	struct heracles *heracles = inet_csk_ca(sk);

	// important because of initial slow start
	u32 window_jump = min(tp->snd_ssthresh, tp->snd_cwnd);	

	heracles_try_enter_ca(heracles, window_jump);

	tp->snd_ssthresh = heracles_ssthresh_estimate(heracles);
	//tp->snd_cwnd = min(tp->snd_cwnd_clamp, ssthresh);
	// heracles_update_group_ssthresh(heracles, tp->snd_ssthresh);   <- dont do it, after a join event, other connections must drop according to the initial slow start

	heracles->events_ts[HER_JOIN] = heracles->group->events_ts[HER_JOIN];
	heracles->events_ts[HER_LEAVE] = heracles->group->events_ts[HER_LEAVE];
	heracles->events_ts[HER_LOSS] = heracles->group->events_ts[HER_LOSS];


	heracles_add_event(heracles, HER_JOIN);
}

// heracles_ss_skip: when cwnd < ssthresh, tries to insert connection in a group
//
//
bool heracles_ss_skip(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct heracles *heracles = inet_csk_ca(sk);

	if (!heracles->group && heracles->acks >= MIN_ACKS) {
	//if (heracles_ssthresh_estimate(heracles) > tp->snd_ssthresh && heracles->acks >= MIN_ACKS) {
		hydra_add_node(heracles);
		// Slow Start Skip Conditions
		//	1. connection is in a group (atleast 3 acks for RTT estimation)
		//	2. alteast another connection is in Congestion Avoidance
		//	3. Jumping slow start will increase the connection's congestion window


		//experiment instead of the following:
		if (heracles->group->in_ca_count > 0 && heracles_ssthresh_estimate(heracles) > tp->snd_cwnd) {
		//if (heracles->group->in_ca_count > 0 && heracles_ssthresh_estimate(heracles) > tp->snd_ssthresh) {
			printk(KERN_INFO "SKIPPING SS - totalss:%d ss:%d cwnd:%d group_size:%d in_ca_count:%d \n", heracles->group->ssthresh_total, tp->snd_ssthresh, tp->snd_cwnd, heracles->group->size, heracles->group->in_ca_count);
			heracles_group_skip(sk);
			return true;
		}
	}
	return false;
}


void heracles_ca(struct sock *sk) 
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct heracles *heracles = inet_csk_ca(sk);


	// Connection tries to join group during Congestion avoidance
	if (!heracles->group && heracles->acks >= MIN_ACKS) {
		// Check if joined a group successfully
		if (hydra_add_node(heracles)) {
			heracles_group_skip(sk);
		}
		return;
	}
	
	if (!heracles->group)
		return;

	heracles_try_enter_ca(heracles, tp->snd_ssthresh);

	heracles_event_handling(sk);
}



void tcp_heracles_cong_avoid(struct sock *sk, u32 ack, u32 acked)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct heracles *heracles = inet_csk_ca(sk);

	HERACLES_SOCK_DEBUG(tp, heracles);

	if (!tcp_is_cwnd_limited(sk))
		return;

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

	// Increase the congestion window
	if (heracles->group)
		heracles_update_group_cwnd(heracles, tp->snd_cwnd);

	/* This should be the last instruction:
	 *  - Connection may have no group and join, having a higher sthresh, and will increase cwnd exponentially
	 		(I know, it is strange that a connection, which is aleady in CA, will increase its ssthresh even more
	 		after joining a group, this is because it suffers a loss soon, having a really low ssthresh,
	 		from which it rises)
	 */
	heracles_ca(sk);

}
EXPORT_SYMBOL_GPL(tcp_heracles_cong_avoid);


void tcp_heracles_pkts_acked(struct sock *sk, u32 acked, s32 rtt)
{
	struct heracles *heracles = inet_csk_ca(sk);
	heracles->acks += acked;
	heracles->rtt = rtt;

	if (!heracles->group) return;

	if (!hydra_remains_in_group(heracles)) {
		heracles->acks = acked;
		heracles_update_group_cwnd(heracles, 0);
		heracles_try_leave_ca(heracles);
	}
	hydra_update(heracles);

}
EXPORT_SYMBOL_GPL(tcp_heracles_pkts_acked);

u32 tcp_reno2_ssthresh(struct sock *sk)
{
	const struct tcp_sock *tp = tcp_sk(sk);
	struct heracles *h = inet_csk_ca(sk);

	if (!tcp_in_initial_slowstart(tp) && h->group) {

		u32 new_ssthresh = max(tp->snd_cwnd >> 1U, 2U);
		heracles_update_group_ssthresh(h, new_ssthresh);
		heracles_add_event(h, HER_LOSS);
		return max(heracles_ssthresh_estimate(h), 2U);
	}
	return max(tp->snd_cwnd >> 1U, 2U); 
}

EXPORT_SYMBOL(tcp_reno2_ssthresh);

struct tcp_congestion_ops tcp_heracles = {
	.init		= tcp_heracles_init,
	.flags		= TCP_CONG_NON_RESTRICTED,
	.name		= "heracles",
	.owner		= THIS_MODULE,
	.ssthresh	= tcp_reno2_ssthresh,
	.cong_avoid	= tcp_heracles_cong_avoid,
	.pkts_acked	= tcp_heracles_pkts_acked,
	.cwnd_event	= tcp_heracles_cwnd_event,
	.release	= tcp_heracles_release,
};


static int __init heracles_init(void)
{
	BUILD_BUG_ON(sizeof(struct heracles) > ICSK_CA_PRIV_SIZE);
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
