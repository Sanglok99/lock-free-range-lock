#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/printk.h>

#include "lockfree_list.h"

LIST_HEAD(thread_workers); // thread_workers: global list head for worker threads

extern int thread_task(void *data);

static int __init lockfree_rangelock_init(void)
{
	int nr_workers = num_online_cpus();
	struct ListRL *list_rl = kmalloc(sizeof(struct ListRL), GFP_KERNEL);

	pr_info("Lock-free range lock module loading: %d workers\n", nr_workers);
	
	int i;
	for (i = 0; i < nr_workers; i++) {
		struct thread_worker *worker = kmalloc(sizeof(struct thread_worker), GFP_KERNEL);
		if (!worker) {
			pr_err("Failed to allocate memory for worker %d\n", i);
			continue;
		}
		worker->worker_id = i; 
		worker->range_start = (2 * i) % nr_workers;
		worker->range_end = (2 * i + 1) % nr_workers;
		worker->task = kthread_run(thread_task, worker, "lf_list-worker%d", i);
		if (IS_ERR(worker->task)) {
			kfree(worker);
			pr_err("Worker %d failed to start.\n", i);
			break;
		}
		worker->list_rl = list_rl;
		list_add_tail(&worker->worker_list, &thread_workers);
		pr_info("Successfully added worker %d in thread_workers list\n", i);
	}

	pr_info("Lock-free range lock module successfully loaded.\n");
	return 0;
}

static void __exit lockfree_rangelock_exit(void)
{
	struct thread_worker *worker, *tmp;
	struct ListRL *list_rl = NULL;
	list_for_each_entry_safe(worker, tmp, &thread_workers, worker_list) {
		kthread_stop(worker->task);
		list_del(&worker->worker_list);
		if (!list_rl)
			list_rl = worker->list_rl;
		kfree(worker);
	}
	kfree(list_rl);
	pr_info("Lock-free range lock module successfully removed.\n");
}

module_init(lockfree_rangelock_init);
module_exit(lockfree_rangelock_exit);

MODULE_AUTHOR("Sanglok Lee");
MODULE_DESCRIPTION("Lock-free range lock module");
MODULE_LICENSE("GPL");
