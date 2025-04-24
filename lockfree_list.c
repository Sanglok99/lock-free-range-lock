#include "lockfree_list.h"
#include <linux/kthread.h> // kthread_should_stop
#include <linux/delay.h> // msleep, msleep_interruptible
#include <asm/cmpxchg.h>
#include <linux/random.h> // get_random_u32()

#define TRY_ONCE false

extern struct list_head thread_workers;

void DeleteNode(struct LNode* lock);

struct LNode* InitNode(unsigned long long start, unsigned long long end, bool reader)
{
	struct LNode* ret = kmalloc(sizeof(struct LNode), GFP_KERNEL);
	ret->start = start;
	ret->end = end;
	ret->next = NULL;
	ret->reader = reader;
	return ret;
}

bool marked(volatile struct LNode* node){ // Check if tagged pointer
	return (unsigned long long)(node) & 0x1; // Casting the address stored in the pointer to an integer
}

struct LNode* unmark(volatile struct LNode* node) // Get the original address of LNode from the tagged pointer
{
	return (struct LNode*)((unsigned long long)(node) & 0xFFFFFFFFFFFFFFFE); // Clear the least significant bit (LSB) using bitmask 0xFFFFFFFFFFFFFFFE
}

int r_validate(struct LNode* lock, bool try_once)
{
	volatile struct LNode** prev = &lock->next;
	struct LNode* cur = unmark(*prev);
	while (true) {
		if (!cur) {
			return 0;
		}
		if (cur == lock) {
			return 0;
		}
		if (marked(cur->next)) {
			struct LNode* next = unmark(cur->next);
			if (cmpxchg(prev, cur, next) == cur) {
				kfree_rcu(cur, rcu);
			}
			cur = next;
		} else if (cur->reader) {
			prev = &cur->next;
			cur = unmark(*prev);
		} else {
			if (try_once) {
				return -1;
			}
			while (!marked(cur->next)) {
				cur = *prev;
			}
		}
	}
}

int w_validate(volatile struct LNode** listrl, struct LNode* lock)
{
	volatile struct LNode** prev = listrl;
	struct LNode* cur = unmark(*prev);
	while (true) {
		if (!cur) {
			return 0;
		}
		if (cur == lock) {
			return 0;
		}
		if (marked(cur->next)) {
			struct LNode* next = unmark(cur->next);
			if (cmpxchg(prev, cur, next) == cur) {
				kfree_rcu(cur, rcu);
			}
			cur = next;
		} else {
			if (cur->end <= lock->start) {
				prev = &cur->next;
				cur = unmark(*prev);
			} else {
				DeleteNode(lock);
				return 1;
			}
		}
	}
}

int compareRW(struct LNode* lock1, struct LNode* lock2)
{
	if (!lock1) {
		return 1;
	} else {
		int readers = lock1->reader + lock2->reader;
		if (lock2->start >= lock1->end) {
			return -1;
		}
		if (lock2->start >= lock1->start && readers == 2) { // both lock1 and lock2 are readers
			return -1;
		}
		if (lock1->start >= lock2->end) {
			return 1;
		}
		if (lock1->start >= lock2->start && readers == 2) {
			return 1;
		}
		return 0;
	}
}

int InsertNodeRW(volatile struct LNode** listrl, struct LNode* lock, bool try_once) 
{
	rcu_read_lock();
	while (true) {
		volatile struct LNode** prev = listrl;
		struct LNode* cur = *prev;
		while (true) {
			if (marked(cur)){ // If current is logically deleted, try again from the head
				break;
			}
			else {
				if (cur && marked(cur->next)) { // Case 1) If cur->next is logically deleted...
					struct LNode* next = unmark(cur->next); // Decode LNode address from tagged pointer
					if (cmpxchg(prev, cur, next) == cur) { // Physically delete when no more reference
						kfree_rcu(cur, rcu);
					}
					cur = next;
				}
				else { // Case 2) If cur is not logically deleted...
					int ret = compareRW(cur, lock); /* Compare range of cur and lock;
									-1: cur < lock
									0: cur == lock (conflict)
									+1: cur > lock */
					if (ret == -1) { // Case A) cur < lock; Proceed to the next lock in this bucket
						prev = &cur->next;
						cur = *prev;
					} else if (ret == 0) { // Case B) Locks conflict each other
						if (try_once) {
							rcu_read_unlock();
							return -1;
						}
						while (!marked(cur->next)) {
							cur = *prev;
						}
					} else if (ret == 1) { // Case C) lock < cur; Found where to insert lock
						lock->next = cur;
						if (cmpxchg(prev, cur, lock) == cur) { // Try to insert new lock before cur
							// Insertion succeed; validate the CAS result
							int ret = 0; 
							if (lock->reader) {
								ret = r_validate(lock, try_once);
							} else {
								ret = w_validate(listrl, lock);
							}
							rcu_read_unlock();
							return ret;
						}
						cur = *prev;
					}
				}
			}
		}
	}
	rcu_read_unlock();
	return -1;
}

struct RangeLock* RWRangeAcquire(struct ListRL* list_rl,
		unsigned long long start, unsigned long long end, bool reader)
{
	struct RangeLock* rl = kmalloc(sizeof(struct RangeLock), GFP_KERNEL);
	int ret = 0;
#if HASH_MODE
	if (end == MAX_SIZE) {
		assert(start == 0);
		rl->bucket = ALL_RANGE;
		for (int i = 0 ; i < BUCKET_CNT ; i++) {
			rl->node[i] = InitNode(0, MAX_SIZE, reader);
			do {
				ret = InsertNodeRW(&list_rl->head[i], rl->node[i], TRY_ONCE);
			} while(ret);
		}
	} else {
		int i = start % BUCKET_CNT;
		assert(start + 1 == end);
		rl->bucket = i;
		rl->node[i] = InitNode(start, end, reader);
		do {
			ret = InsertNodeRW(&list_rl->head[i], rl->node[i], TRY_ONCE);
		} while(ret);
	}
	return rl;
#else
	rl->node = InitNode(start, end, reader);
	do {
		ret = InsertNodeRW(&list_rl->head, rl->node, TRY_ONCE);
	} while(ret); // Repeat while ret==1

	return rl;
#endif
}

void DeleteNode(struct LNode* lock)
{
	while (true) {
		volatile struct LNode* orig = lock->next;
		unsigned long long marked = (unsigned long long)orig + 1; // marking
		if (cmpxchg(&lock->next, orig, (struct LNode*)marked) == orig) {
			break;
		}
	}
}

void MutexRangeRelease(struct RangeLock* rl)
{
#if HASH_MODE
	if (rl->bucket == ALL_RANGE) { // Case 1) Logically delete All-Range Lock
		for (int i = 0 ; i < BUCKET_CNT ; i++) {
			DeleteNode(rl->node[i]);
		}
	} else { // Case 2) Logically delete one Range Lock
		DeleteNode(rl->node[rl->bucket]);
	}
	kfree(rl); // Physically delete Range Lock
#else
	DeleteNode(rl->node);
	kfree(rl);
#endif
}

int thread_task(void *data)
{
	struct thread_worker *worker = data;	
	struct RangeLock* lock;
	int range_start = worker->range_start;
	int range_end = worker->range_end;
	int count = 0;
	while (!kthread_should_stop()) {
		if(msleep_interruptible(500)){
			break;
		}

		bool reader = (get_random_u32() % 2) == 0;  // randomly allocates reader or writer
		lock = RWRangeAcquire(worker->list_rl, range_start, range_end, false);
		pr_info("[worker %d] Inserted node(range: %d - %d, %s)\n", worker->worker_id, range_start, range_end, lock->node->reader ? "reader" : "writer");
		
		BUG_ON(!lock);

		if(msleep_interruptible(1000)){
			break;
		}

		MutexRangeRelease(lock);
		count++;

		if(msleep_interruptible(1000)){
			break;
		}
	}
	pr_info("[worker %d] lock cycles completed: %d\n", worker->worker_id, count);
	return 0;
}
