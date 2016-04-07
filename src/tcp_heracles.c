/*
 * TCP Heracles congestion control
 *
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/inet_diag.h>

#include <net/tcp.h>

#include "tcp_heracles.h"
#include "hydra.h"


//http://stackoverflow.com/questions/3060950/how-to-get-ip-address-from-sock-structure-in-c


void tcp_heracles_init(struct sock *sk)
{
	struct heracles *heracles = inet_csk_ca(sk);

	BUG_ON(!hydra);

	//need to find a group for this socket
	//need to be sure that, on restart, the previous iteration of this connection was correctly removed from its group
	heracles->group = hydra_add_node(hydra, heracles);	
}


EXPORT_SYMBOL_GPL(tcp_heracles_init);


void tcp_heracles_cwnd_event(struct sock *sk, enum tcp_ca_event event)
{
	//vegas does this to erase previous calculations, should adapt it
	// heracles may also need to modify rtt for the group (or add a flag indicating that it shouldn't)


	//is it really needed to reset? (like below)
	//won't calling hydra_update after a new rtt make the connection move group
	//need to know if being in the wrong group during that time (from restart until having the first acknowledgements)

	// probably the only reset required is to recount the samples up to slow start, then it can be updated into a group


	if (event == CA_EVENT_CWND_RESTART ||
	    event == CA_EVENT_TX_START) {
	    	struct heracles *heracles = inet_csk_ca(sk);
		hydra_remove_node(heracles);
		tcp_heracles_init(sk);
	}
		
}
EXPORT_SYMBOL_GPL(tcp_vegas_cwnd_event);

void tcp_vegas_pkts_acked(struct sock *sk, u32 acked, s32 rtt)
{
	struct heracles *heracles = inet_csk_ca(sk);

	// need to check if rtt will make connection change group

}

EXPORT_SYMBOL_GPL(tcp_heracles_pkts_acked);


/*
 * Drop ssthresh for this connection (as if TCP-NEwRENO), changing for entire group 
 * will cause possible secondary effects
 */
static inline u32 tcp_heracles_ssthresh(struct tcp_sock *tp)
{
	return  min(tp->snd_ssthresh >> 1U, 2U);
}
/*
 * 
 */
static void tcp_heracles_cong_avoid(struct sock *sk, u32 ack, u32 acked)
{
	
}

/* Extract info for Tcp socket info provided via netlink. */
size_t tcp_vegas_get_info(struct sock *sk, u32 ext, int *attr,
			  union tcp_cc_info *info)
{

}
EXPORT_SYMBOL_GPL(tcp_vegas_get_info);

static struct tcp_congestion_ops tcp_heracles __read_mostly = {
	.init		= tcp_heracles_state_init,
	.ssthresh	= tcp_reno_ssthresh,
	.cong_avoid	= tcp_vegas_cong_avoid,
	.pkts_acked	= tcp_vegas_pkts_acked,
	.set_state	= tcp_vegas_state,
	.cwnd_event	= tcp_vegas_cwnd_event,
	//.get_info	= tcp_vegas_get_info,
	.owner		= THIS_MODULE,
	.name		= "heracles",
};


//declare hydra structure
HYDRA_DECLARE(hydra); 


static int __init tcp_heracles_register(void)
{
	//do define in header
	hydra_init(hydra);	// initializes hydra structure
	tcp_register_congestion_control(&tcp_heracles);
	return 0;
}


static void __exit tcp_heracles_unregister(void)
{
	//tcp_heracles_delete_hydra(hydra);
	tcp_unregister_congestion_control(&tcp_heracles);
}

module_init(tcp_heracles_register);
module_exit(tcp_heracles_unregister);

MODULE_AUTHOR("Pedro Braz");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TCP Heracles");
