#ifndef LOCKFREE_LIST_H
#define LOCKFREE_LIST_H

#include <linux/list.h> // list_head
#include <linux/slab.h>
#include <linux/cpumask.h>
#include <linux/sched.h>

#define HASH_MODE 0
#define IN_KERNEL2 1

#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)

extern struct list_head test_workers;

struct ListRL {
#if HASH_MODE
	volatile struct LNode* head[BUCKET_CNT];
#else
	volatile struct LNode* head;
#endif
};

struct LNode {
	unsigned int start;
	unsigned int end;
	volatile struct LNode* next;
	unsigned int reader;
#if IN_KERNEL2
	struct rcu_head rcu;
#endif
};

struct RangeLock {
#if HASH_MODE
	struct LNode* node[BUCKET_CNT];
	unsigned int bucket;
#else
	struct LNode* node;
#endif
};

struct test_worker {
	int range_start;
	int range_end;
	struct ListRL *list_rl;
	int worker_id;
	struct task_struct *task;
	struct list_head worker_list; 
};

#endif
