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
void heracles_add_event(struct heracles*);
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
		.is_limited=0,
		.excess=0,
		.in_ca=0,
		.old_ssthresh=0,
		.old_cwnd=0,
		.ts=0,
	};
}
EXPORT_SYMBOL_GPL(tcp_heracles_init);

void tcp_heracles_release(struct sock *sk) 
{
	struct heracles *heracles = inet_csk_ca(sk);
	if (heracles->group) {

		//	node leaves group without removing ssthresh information
		// 	signals event, so other connections can get the ssthresh that was released

	    heracles_in_group_release(heracles);
	    heracles_add_event(heracles);
		hydra_remove_node(heracles);
	}
}
EXPORT_SYMBOL_GPL(tcp_heracles_release);


void tcp_heracles_cwnd_event(struct sock *sk, enum tcp_ca_event event)
{
	if (event == CA_EVENT_CWND_RESTART ||
	    event == CA_EVENT_TX_START) {

		tcp_heracles_release(sk);
		tcp_heracles_init(sk);
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

void heracles_add_event(struct heracles *heracles) 
{
	heracles->group->ts++;
	//heracles->ts = heracles->group->ts;
}
bool heracles_is_event(struct heracles *heracles) 
{
	bool is_event = heracles->group->ts > heracles->ts;
	if (is_event) 
		heracles->ts = heracles->group->ts;
	return is_event;
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
	//if (heracles->old_ssthresh != 0) printk(KERN_INFO "Problem here");

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

static inline u32 heracles_ssthresh_estimate(struct heracles *heracles)
{
	//printk(KERN_INFO "ss estimation for %p: (ss - %u; size - %u;) \n", heracles, heracles->group->ssthresh_total, heracles->group->size);
	return heracles->group->ssthresh_total / heracles->group->size;
}

static inline u32 heracles_cwnd_estimate(struct heracles *heracles)
{
	return heracles->group->cwnd_total / heracles->group->size;
}

void tcp_heracles_cong_avoid(struct sock *sk, u32 ack, u32 acked)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct heracles *heracles = inet_csk_ca(sk);

	HERACLES_SOCK_DEBUG(tp, heracles);
	
	// If the connection is looking for a group
	if (!heracles->group && heracles->acks >= MIN_ACKS)
		hydra_add_node(heracles);	


	if (!tcp_is_cwnd_limited(sk))
		return;

	/* In "safe" area, increase. */
	if (tp->snd_cwnd < tp->snd_ssthresh) {
		heracles_try_leave_ca(heracles);

		// Slow Start Skip Conditions
		//	1. connection is in a group (atleast 3 acks for RTT estimation)
		//	2. alteast another connection is in Congestion Avoidance
		//	3. Jumping slow start will increase the connection's congestion window

		if (heracles->group
			&& heracles->group->in_ca_count > 0 
			&& heracles_ssthresh_estimate(heracles) > tp->snd_cwnd) {
			printk(KERN_INFO "SSS: her:%p ss:%u\n", heracles, heracles_ssthresh_estimate(heracles));
			// When a connection enters CA from SSS:
			//	1. add its own ssthresh to the group (heracles_try_enter_ca)
			// 	2. gets is own estimate for ssthresh (c1 + c2 + ... + cn) / N
			//	3. Updates its CWND
			//	4. Updates group ssthresh
			//	5. Signals event to group

			heracles_try_enter_ca(heracles, tp->snd_ssthresh);

			u32 ssthresh = heracles_ssthresh_estimate(heracles);
			tp->snd_cwnd = min(tp->snd_cwnd_clamp, ssthresh);
			heracles_update_group_ssthresh(heracles, ssthresh);
			heracles_add_event(heracles);
			return;
		
		} else {
			acked = tcp_reno2_slow_start(tp, acked);
			if (!acked)
				return;
		}
	}

	/* In dangerous area, increase slowly. */
	if (heracles->group) {
		heracles_try_enter_ca(heracles, tp->snd_ssthresh);

		if (heracles_is_event(heracles) 
			&& tp->snd_cwnd > heracles_ssthresh_estimate(heracles)) {
			
			//printk("There is an event (%p) new ss: %u\n", heracles, heracles_ssthresh_estimate(heracles));
			tp->snd_ssthresh = heracles_ssthresh_estimate(heracles);
			tp->snd_cwnd = tp->snd_ssthresh;
		} else
		if (tp->snd_cwnd < heracles_ssthresh_estimate(heracles)) {
			tp->snd_cwnd = heracles_ssthresh_estimate(heracles);
			heracles_add_event(heracles);	
		}
	}
	tcp_reno2_cong_avoid_ai(tp, tp->snd_cwnd, acked);

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
		//heracles->group->cwnd_total -= heracles->old_cwnd;
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
		heracles_add_event(h);
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
