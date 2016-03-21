#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/inet.h> 

#include "hydra.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pedro Braz");
MODULE_DESCRIPTION("Hydra Tester");


struct heracles {
	struct hydra_group *group; 
	u32 inet_addr;
	u32 rtt;

};

static int __init hydra_tester_init(void)
{

	printk(KERN_INFO );
	return 0;
}

static void __exit hydra_tester_cleanup(void) 
{
	printk(KERN_INFO "Cleaning up\n");
}


void hydra_test1(void) 
{
	HYDRA_DECLARE(hydra);
	hydra_init(hydra);

	struct heracles heracles = {
		.rtt = 10;
		.inet_addr= hton(in_aton("127.0.0.1"));
	};

	hydra_add_node(hydra, &heracles);
	heracles.rtt = 20;
	hydra_update(&heracles);

	hydra_remove_node(&heracles);
}

module_init(hello_init);
module_exit(hello_cleanup);
