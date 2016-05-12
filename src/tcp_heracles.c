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

#define HERACLES_SOCK_DEBUG(tp, her)\
	printk(KERN_INFO "%p %u %d %d %d %d %d %d %d\n", her, her->inet_addr, tp->packets_out, tp->snd_cwnd, tp->snd_ssthresh, tp->mss_cache, her->rtt, tp->srtt_us, tp->mdev_us);

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
	};
	//Cant have a group while initializing, needs rtt estimates first
	//hydra_add_node(heracles);	

}
EXPORT_SYMBOL_GPL(tcp_heracles_init);

void tcp_heracles_release(struct sock *sk) 
{
	struct heracles *heracles = inet_csk_ca(sk);
	if (heracles->group)
		hydra_remove_node(heracles);
}
EXPORT_SYMBOL_GPL(tcp_heracles_release);


void tcp_heracles_cwnd_event(struct sock *sk, enum tcp_ca_event event)
{
	if (event == CA_EVENT_CWND_RESTART ||
	    event == CA_EVENT_TX_START) {
	    struct heracles *heracles = inet_csk_ca(sk);
	    if (heracles->group)
			hydra_remove_node(heracles);
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


void heracles_try_enter_ca(struct heracles *heracles)
{
	if (heracles->in_ca) return;
	printk(KERN_INFO "ENTER CA %p; oldss: %u\n", heracles->old_ssthresh);
	heracles->in_ca = 1;
	heracles->group->in_ca_count++; 
}

void heracles_try_leave_ca(struct heracles *heracles) 
{
	if (!heracles->in_ca) return;
	printk(KERN_INFO "LEAVE CA %p; oldss:%u\n", heracles->old_ssthresh);
	heracles->in_ca = 0;
	heracles->group->in_ca_count--; 
	heracles->group->ssthresh_total -= heracles->old_ssthresh;
	heracles->old_ssthresh = 0;
}

static inline u32 heracles_ssthresh_estimate(struct heracles *heracles)
{
	printk(KERN_INFO "ss estimation for %p: (ss - %u; size - %u;) \n", heracles, heracles->group->ssthresh_total, heracles->group->size);
	return heracles->group->ssthresh_total / heracles->group->size;
}


void tcp_heracles_cong_avoid(struct sock *sk, u32 ack, u32 acked)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct heracles *heracles = inet_csk_ca(sk);


	HERACLES_SOCK_DEBUG(tp, heracles);

	/*
	 * If the connection is looking for a group
	 */
	if (!heracles->group && heracles->acks >= MIN_ACKS)
		hydra_add_node(heracles);	


	if (!tcp_is_cwnd_limited(sk))
		return;

	/* In "safe" area, increase. */
	if (tp->snd_cwnd <= tp->snd_ssthresh) {
		heracles_try_leave_ca(heracles);

		/* if there is atleast 1 connection on congestion avoidance 
		 * and this connection already has a good SRTT sample, skip slow start
		 */
		 //IT is impossible for heracles to be in CA here!!
		if (heracles->group && heracles->group->in_ca_count > 0 && heracles->acks >= MIN_ACKS) {
			printk(KERN_INFO "SS SKIP %p;  in_ca: %d\n", heracles, heracles->group->in_ca_count);
			tp->snd_ssthresh = heracles_ssthresh_estimate(heracles);
			tp->snd_cwnd = min(tp->snd_cwnd_clamp, tp->snd_ssthresh);

			heracles_try_enter_ca(heracles);
			heracles->group->ssthresh_total += tp->snd_ssthresh;
			heracles->old_ssthresh = tp->snd_ssthresh;
			return;

		} else {
			acked = tcp_reno2_slow_start(tp, acked);
			if (!acked)
				return;
		}
	}

	/* In dangerous area, increase slowly. */
	if (heracles->group) {
		heracles_try_enter_ca(heracles);
		/*update group ssthresh*/
		heracles->group->ssthresh_total -= heracles->old_ssthresh;
		heracles->group->ssthresh_total += tp->snd_ssthresh;
		heracles->old_ssthresh = tp->snd_ssthresh;	
	}	

	tcp_reno2_cong_avoid_ai(tp, tp->snd_cwnd, acked);
}
EXPORT_SYMBOL_GPL(tcp_heracles_cong_avoid);

void tcp_heracles_pkts_acked(struct sock *sk, u32 acked, s32 rtt)
{
	struct heracles *heracles = inet_csk_ca(sk);
	//struct tcp_sock *tp = tcp_sk(sk);

	heracles->acks += acked;
	heracles->rtt = rtt;

	if (!heracles->group) return;

	if (!hydra_remains_in_group(heracles)) {
		/* do cleanup in old group before updating */
		heracles->acks = acked; 	
		heracles_try_leave_ca(heracles);
		printk(KERN_INFO "CHANGING %p; g:%p\n", heracles, heracles->group);
	}
	else printk(KERN_INFO "NOT CHANGING GROUP %p; g:%p \n", heracles, heracles->group); /*else if (heracles->acks >= MIN_ACKS){	
		
		if (heracles->group->in_ca_count)
			tp->ssthresh = heracles->group->ssthresh_total 
					/ (group->in_ca + 1);
	}*/
	hydra_update(heracles);

}
EXPORT_SYMBOL_GPL(tcp_heracles_pkts_acked);

u32 tcp_reno2_ssthresh(struct sock *sk)
{
	const struct tcp_sock *tp = tcp_sk(sk);
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
	printk(KERN_INFO "Initializing our hero and savior: HERACLES!!");
	tcp_register_congestion_control(&tcp_heracles);
	return 0;
}



static void __exit heracles_exit(void)
{
	tcp_unregister_congestion_control(&tcp_heracles);
	printk(KERN_INFO "Heracles cleanup success.");
}


module_init(heracles_init);
module_exit(heracles_exit);
