#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <net/tcp.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pedro Braz");
MODULE_DESCRIPTION("TCP Reno from v3.16 for logging");

struct reno_debug {
	u32 rtt;
};

#define RENO_SOCK_DEBUG(tp, deb)\
	printk(KERN_INFO "%p %u %d %d %d %d %d %d %d\n", deb, 0, tp->packets_out, tp->snd_cwnd, tp->snd_ssthresh, tp->mss_cache, deb->rtt, tp->srtt_us, tp->mdev_us);

void tcp_reno_debug_init(struct sock *sk)
{
	struct reno_debug *d= inet_csk_ca(sk);
	d->rtt = 0;
}
EXPORT_SYMBOL_GPL(tcp_reno_debug_init);


void tcp_reno_debug_cong_avoid(struct sock *sk, u32 ack, u32 acked)
{
        struct tcp_sock *tp = tcp_sk(sk);
        struct reno_debug *d = inet_csk_ca(sk);
        RENO_SOCK_DEBUG(tp, d);

        if (!tcp_is_cwnd_limited(sk))
                return;

        /* In "safe" area, increase. */
        if (tp->snd_cwnd <= tp->snd_ssthresh)
                tcp_slow_start(tp, acked);
        /* In dangerous area, increase slowly. */
        else
                tcp_cong_avoid_ai(tp, tp->snd_cwnd);
}
EXPORT_SYMBOL_GPL(tcp_reno_debug_cong_avoid);


void tcp_reno_debug_pkts_acked(struct sock *sk, u32 acked, s32 rtt)
{
        struct reno_debug *reno_debug = inet_csk_ca(sk);
        reno_debug->rtt = rtt;
}
EXPORT_SYMBOL_GPL(tcp_reno_debug_pkts_acked);

struct tcp_congestion_ops tcp_reno_debug = {
        .flags          = TCP_CONG_NON_RESTRICTED,
        .name           = "reno_debug",
        .init 		= tcp_reno_debug_init,
        .pkts_acked     = tcp_reno_debug_pkts_acked,
        .owner          = THIS_MODULE,
        .ssthresh       = tcp_reno_ssthresh,
        .cong_avoid     = tcp_reno_debug_cong_avoid,
};


static int __init reno_debug_init(void)
{
	BUILD_BUG_ON(sizeof(struct reno_debug) > ICSK_CA_PRIV_SIZE);
	printk(KERN_INFO "Initializing Reno debug...");
	tcp_register_congestion_control(&tcp_reno_debug);
	return 0;
}



static void __exit reno_debug_exit(void)
{
	tcp_unregister_congestion_control(&tcp_reno_debug);
	printk(KERN_INFO "Reno debug cleanup success.");
}


module_init(reno_debug_init);
module_exit(reno_debug_exit);
