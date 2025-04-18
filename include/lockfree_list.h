#define ALL_RANGE (0xFFFFFFFF)
#define MAX_SIZE (0xFFFFFFFF)

struct RangeLock* RWRangeAcquire(struct ListRL* list_rl,
unsigned long long start, unsigned long long end, bool writer);

struct RangeLock* RWRangeTryAcquire(struct ListRL* list_rl,
unsigned long long start, unsigned long long end, bool writer);

void MutexRangeRelease(struct RangeLock* rl);

struct ListRL { // Hash Bucket
#if HASH_MODE
volatile struct LNode* head[BUCKET_CNT];
#else
volatile struct LNode* head;
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

struct LNode {
	unsigned int start;
	unsigned int end;
	volatile struct LNode* next;
	unsigned int reader;
#if IN_KERNEL2
	struct rcu_head rcu;
#endif
};


