#include "include/lockfree_list.h"

struct RangeLock* RWRangeAcquire(struct ListRL* list_rl,
		unsigned long long start, unsigned long long end, bool writer)
{
	struct RangeLock* rl = kmalloc(sizeof(struct RangeLock), GFP_KERNEL);
	int ret = 0;
#if HASH_MODE
	if (end == MAX_SIZE) {
		assert(start == 0);
		rl->bucket = ALL_RANGE;
		for (int i = 0 ; i < BUCKET_CNT ; i++) {
			rl->node[i] = InitNode(0, MAX_SIZE, writer);
			do {
				ret = InsertNodeRW(&list_rl->head[i], rl->node[i], false);
			} while(ret);
		}
	} else {
		int i = start % BUCKET_CNT;
		assert(start + 1 == end);
		rl->bucket = i;
		rl->node[i] = InitNode(start, end, writer);
		do {
			ret = InsertNodeRW(&list_rl->head[i], rl->node[i], false);
		} while(ret);
	}
	return rl;
#else
	rl->node = InitNode(start, end, writer);
	do {
		ret = InsertNodeRW(&list_rl->head, rl->node, false);
	} while(ret); 
	return rl;
#endif
}

struct LNode* InitNode(unsigned long long start, unsigned long long end, bool writer)
{
	struct LNode* ret = kmalloc(sizeof(struct LNode), GFP_KERNEL);
	ret->start = start;
	ret->end = end;
	ret->next = NULL;
	ret->reader = !writer;
	return ret;
}

void DeleteNode(struct LNode* lock)
{
	while (true) {
		volatile struct LNode* orig = lock->next;
		unsigned long long marked = (unsigned long long)orig + 1;
		if (cmpxchg(&lock->next, orig, (struct LNode*)marked) == orig) {
			break;
		}
	}
}

struct LNode* unmark(volatile struct LNode* node)
{
	return (struct LNode*)((unsigned long long)(node) & 0xFFFFFFFFFFFFFFFE);
}

int InsertNodeRW(volatile struct LNode** listrl, struct LNode* lock, bool try) 
{
	rcu_read_lock();
	while (true) {
		volatile struct LNode** prev = listrl;
		struct LNode* cur = *prev;
		while (true) {
			if (marked(cur)){
				break;
			}
			else {
				if (cur && marked(cur->next)) {
					struct LNode* next = unmark(cur->next);
					if (cmpxchg(prev, cur, next) == cur) {
						kfree_rcu(cur, rcu);
					}
					cur = next;
				}
				else {
					int ret = compareRW(cur, lock);
					if (ret == -1) {
						prev = &cur->next;
						cur = *prev;
					} else if (ret == 0) {
						if (try) {
							rcu_read_unlock();
							return -1;
						}
						while (!marked(cur->next)) {
							cur = *prev;
						}
					} else if (ret == 1) {
						lock->next = cur;
						if (cmpxchg(prev, cur, lock) == cur) {
							int ret = 0;
							if (lock->reader) {
								ret = r_validate(lock, try);
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

int compareRW(struct LNode* lock1, struct LNode* lock2)
{
	if (!lock1) {
		return 1;
	} else {
		int readers = lock1->reader + lock2->reader;
		if (lock2->start >= lock1->end) {
			return -1;
		}
		if (lock2->start >= lock1->start && readers == 2) {
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

int r_validate(struct LNode* lock, bool try)
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
			if (try) {
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

void MutexRangeRelease(struct RangeLock* rl)
{
#if HASH_MODE
	if (rl->bucket == ALL_RANGE) {
		for (int i = 0 ; i < BUCKET_CNT ; i++) {
			DeleteNode(rl->node[i]);
		}
	} else {
		DeleteNode(rl->node[rl->bucket]);
	}
	kfree(rl);
#else
	DeleteNode(rl->node);
	kfree(rl);
#endif
}
