#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/inet.h>

#include "hydra.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pedro Braz");
MODULE_DESCRIPTION("Hydra Tester");




void hydra_test1(void);
void hydra_test2(void);
void hydra_test3(void);
void hydra_test4(void);

static int __init hydra_tester_init(void)
{

	printk(KERN_INFO "Initializing module\n");
	hydra_test1();
	hydra_test2();
	hydra_test3();
	hydra_test4();
	return 0;
}

static void __exit hydra_tester_cleanup(void)
{
	printk(KERN_INFO "Cleaning up\n");
}



/*
 * simple single threaded test for Hydra
 */
void hydra_test1(void)
{
	
	struct heracles h1 = {NULL, in_aton("129.0.0.1"), 10};
	hydra_add_node(&h1);
	h1.rtt = 20;
	hydra_update(&h1);
	hydra_remove_node(&h1);
}
/*
 * 2 nodes will be inserted in the same hash list, but different trees
 *
 */
void hydra_test2(void)
{
	struct heracles h1 = {NULL, in_aton("100.5.5.10"), 10};
	struct heracles h2 = {NULL, in_aton("120.6.5.10"), 20};
	hydra_add_node(&h1);	
	hydra_add_node(&h2);
	hydra_remove_node(&h1); // removes 1 group + clears 1st subnet
	hydra_remove_node(&h2); // removes 2 group + clears 2nd subnet
}

/*
 * 2 nodes will be inserted in the same tree, but different groups
 */
void hydra_test3(void)
{
	struct heracles h1 = {NULL, in_aton("120.6.5.10"), 10};
	struct heracles h2 = {NULL, in_aton("120.6.5.11"), 20};
	hydra_add_node(&h1);	
	hydra_add_node(&h2);
	hydra_remove_node(&h1); // removes 1st group
	hydra_remove_node(&h2); // removes 2nd group and clears only subnet
}

/*
 * 2 nodes will be inserted in the same group
 */
void hydra_test4(void)
{
	struct heracles h1 = {NULL, in_aton("120.6.5.10"), 10};
	struct heracles h2 = {NULL, in_aton("120.6.5.11"), 10};
	hydra_add_node(&h1);	
	hydra_add_node(&h2);
	hydra_remove_node(&h1); // removes 1st group
	hydra_remove_node(&h2); // removes 2nd group and clears only subnet
}




//preciso de mais testes

module_init(hydra_tester_init);
module_exit(hydra_tester_cleanup);
