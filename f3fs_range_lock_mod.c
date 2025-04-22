#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/errno.h>

#include "lockfree_list.h"

LIST_HEAD(test_workers); // test_workers: global list head for worker threads

extern int test0_thread1(void *data);

static int __init test_lockfree_range_lock_mod_init(void)
{
	int nr_workers = num_online_cpus();
	// worker_test_fn_t test_fn; 
	int i;
	struct ListRL *list_rl = kmalloc(sizeof(struct ListRL), GFP_KERNEL);
	for (i = 0; i < nr_workers; i++) {
		struct test_worker *worker = kmalloc(sizeof(struct test_worker), GFP_KERNEL);
		worker->worker_id = i;
		worker->range_start = (2 * i) % nr_workers;
		worker->range_end = (2 * i + 1) % nr_workers;
		if (!worker) {
			continue;
		}
		worker->task = kthread_run(test0_thread1, worker, "lf_list-worker%d", i);
		if (IS_ERR(worker->task)) {
			kfree(worker);
			break;
		}
		worker->list_rl = list_rl;
		list_add_tail(&worker->worker_list, &test_workers);
	}
	return 0;
}

static void test_lockfree_range_lock_mod_cleanup(void)
{
	struct test_worker *worker, *tmp;
	struct ListRL *list_rl = NULL;
	list_for_each_entry_safe(worker, tmp, &test_workers, worker_list) {
		kthread_stop(worker->task);
		list_del(&worker->worker_list);
		if (!list_rl)
			list_rl = worker->list_rl;
		kfree(worker);
	}
	kfree(list_rl);
}
