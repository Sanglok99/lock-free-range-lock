#ifndef _F3FS_H
#define _F3FS_H

#include "lockfree_list.h"

static inline struct RangeLock* RWRangeAcquire (struct f3fs_rwsem3* sem)
{
	return __RWRangeAcquire(&sem->list_rl, start, start + size, true);
}

static inline struct RangeLock* f3fs_down_write_trylock3(struct f3fs_rwsem3 *sem)
{
	return RWRangeTryAcquire(&sem->list_rl, 0, MAX_SIZE, true);
}

static inline void f3fs_up_write3(struct RangeLock* range)
{
	MutexRangeRelease(range); 
}

static inline struct RangeLock* f3fs_down_write_range_trylock3(struct f3fs_rwsem3 *sem, unsigned start, unsigned size)
{
	return RWRangeTryAcquire(&sem->list_rl, start, start + size, true);
}

static inline void f3fs_up_write_range3(struct RangeLock* range)
{
	MutexRangeRelease(range);
}

static inline struct RangeLock* f3fs_down_read3(struct f3fs_rwsem3 *sem)
{
	return RWRangeAcquire(&sem->list_rl, 0, MAX_SIZE, false);
}

static inline struct RangeLock* f3fs_down_read_trylock3(struct f3fs_rwsem3 *sem)
{
	return RWRangeTryAcquire(&sem->list_rl, 0, MAX_SIZE, false);
}

static inline void f3fs_up_read3(struct RangeLock* range) 
{
	MutexRangeRelease(range);
}

#endif
