#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <net/tcp.h>

#include "hydra.h"


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pedro Braz");
MODULE_DESCRIPTION("Reno2");

//minimum number of acks for hydra to start 
#define MIN_ACKS 3 

void tcp_heracles_init(struct sock *sk)
{
	struct heracles *heracles = inet_csk_ca(sk);
	*heracles = (struct heracles){
		.group=0,
		.inet_addr=sk->sk_daddr,
		.rtt=0,
		.acks=0,
		.is_limited=0,
		.excess=0
	};
	hydra_add_node(heracles);	

}
EXPORT_SYMBOL_GPL(tcp_heracles_init);


void tcp_heracles_cwnd_event(struct sock *sk, enum tcp_ca_event event)
{
	if (event == CA_EVENT_CWND_RESTART ||
	    event == CA_EVENT_TX_START) {
	    	struct heracles *heracles = inet_csk_ca(sk);
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

void tcp_reno2_cong_avoid(struct sock *sk, u32 ack, u32 acked)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct heracles *heracles = inet_csk_ca(sk);

	if (!tcp_is_cwnd_limited(sk))
		return;


	/* In "safe" area, increase. */
	if (tp->snd_cwnd <= tp->snd_ssthresh) {
		/* if there is atleast 1 connection on congestion avoidance 
		 * and this connection already has a good SRTT sample, skip slow start
		 */
		//if (heracles->group->on_ca > 0 && heracles->acks >= MIN_ACKS)
		//	tcp
		acked = tcp_reno2_slow_start(tp, acked);
		if (!acked)
			return;
	}
	/* In dangerous area, increase slowly. */
	tcp_reno2_cong_avoid_ai(tp, tp->snd_cwnd, acked);
}
EXPORT_SYMBOL_GPL(tcp_reno2_cong_avoid);

void tcp_reno2_pkts_acked(struct sock *sk, u32 acked, s32 rtt)
{
	printk(KERN_INFO "acked: %u - rtt:%d", acked, rtt);

	struct tcp_sock *tp = tcp_sk(sk);
	printk(KERN_INFO "SOCKET INFO:");
	printk(KERN_INFO "srtt: %u - mdev: %u", tp->srtt_us, tp->mdev_us);
	printk(KERN_INFO "maxedev: %u - var: %u", tp->mdev_max_us, tp->rttvar_us);
	printk(KERN_INFO "cwnd: %u - ssthresh: %u", tp->snd_cwnd, tp->snd_ssthresh);

	struct heracles *heracles = inet_csk_ca(sk);

	heracles->acks += acked;
	if (heracles->acks >= MIN_ACKS)	
		//se mudar de group, tenho de heracles->acks = 0
		hydra_update(heracles);

}

u32 tcp_reno2_ssthresh(struct sock *sk)
{
	const struct tcp_sock *tp = tcp_sk(sk);

	return max(tp->snd_cwnd >> 1U, 2U);
}

EXPORT_SYMBOL(tcp_reno2_ssthresh);

struct tcp_congestion_ops tcp_reno = {
	.init		= tcp_heracles_init,
	.flags		= TCP_CONG_NON_RESTRICTED,
	.name		= "reno2",
	.owner		= THIS_MODULE,
	.ssthresh	= tcp_reno2_ssthresh,
	.cong_avoid	= tcp_reno2_cong_avoid,
	.pkts_acked	= tcp_reno2_pkts_acked,
	.cwnd_event	= tcp_heracles_cwnd_event,
};


static int __init reno2_init(void)
{
	printk(KERN_INFO "Inialized reno module");
	tcp_register_congestion_control(&tcp_reno);
	return 0;
}



static void __exit reno2_exit(void)
{
	tcp_unregister_congestion_control(&tcp_reno);
	printk(KERN_INFO "Quitting from reno module");
}


module_init(reno2_init);
module_exit(reno2_exit);
