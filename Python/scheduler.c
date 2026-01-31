#include "Python.h"
#include "pycore_parking_lot.h"
#include "pycore_pystate.h"
#include "pycore_runtime.h"
#include "pycore_scheduler.h"

/* Copied/referenced over from _randommodule.c */

/* initializes mt[N] with a seed */
static void init_genrand(_PyScheduler_RandomObject *self, uint32_t s) {
    const int N = 624;

    int mti;
    uint32_t *mt;

    mt = self->state;
    mt[0]= s;
    for (mti=1; mti<N; mti++) {
        mt[mti] =
        (1812433253U * (mt[mti-1] ^ (mt[mti-1] >> 30)) + mti);
        /* See Knuth TAOCP Vol2. 3rd Ed. P.106 for multiplier. */
        /* In the previous versions, MSBs of the seed affect   */
        /* only MSBs of the array mt[].                        */
        /* 2002/01/09 modified by Makoto Matsumoto             */
    }
    self->index = mti;
    return;
}

/* generates a random number on [0,0xffffffff]-interval */
static uint32_t genrand_uint32(_PyScheduler_RandomObject *self) {
    const int N = 624;
    const int M = 397;
    const unsigned int MATRIX_A = 0x9908b0dfU;
    const unsigned int UPPER_MASK = 0x80000000U;
    const unsigned int LOWER_MASK = 0x7fffffffU;

    uint32_t y;
    static const uint32_t mag01[2] = {0x0U, MATRIX_A};
    /* mag01[x] = x * MATRIX_A  for x=0,1 */
    uint32_t *mt;

    mt = self->state;
    if (self->index >= N) { /* generate N words at one time */
        int kk;

        for (kk=0;kk<N-M;kk++) {
            y = (mt[kk]&UPPER_MASK)|(mt[kk+1]&LOWER_MASK);
            mt[kk] = mt[kk+M] ^ (y >> 1) ^ mag01[y & 0x1U];
        }
        for (;kk<N-1;kk++) {
            y = (mt[kk]&UPPER_MASK)|(mt[kk+1]&LOWER_MASK);
            mt[kk] = mt[kk+(M-N)] ^ (y >> 1) ^ mag01[y & 0x1U];
        }
        y = (mt[N-1]&UPPER_MASK)|(mt[0]&LOWER_MASK);
        mt[N-1] = mt[M-1] ^ (y >> 1) ^ mag01[y & 0x1U];

        self->index = 0;
    }

    y = mt[self->index++];
    y ^= (y >> 11);
    y ^= (y << 7) & 0x9d2c5680U;
    y ^= (y << 15) & 0xefc60000U;
    y ^= (y >> 18);
    return y;
}

/* Referenced from `parking_lot.c` */
typedef struct {
    /* Linked list of `waiter` in this bucket */
    struct llist_node root;
    size_t num_waiters;
} Bucket;

typedef struct {
    /* Address of item this waiter is waiting on */
    uintptr_t addr;
    /* Represents the waiting thread */
    PyThreadState *tstate;
    struct llist_node node;
} waiter;

#define NUM_BUCKETS 257

#define BUCKET_INIT(b, i) [i] = { .root = LLIST_INIT(b[i].root) }
#define BUCKET_INIT_2(b, i)   BUCKET_INIT(b, i),     BUCKET_INIT(b, i+1)
#define BUCKET_INIT_4(b, i)   BUCKET_INIT_2(b, i),   BUCKET_INIT_2(b, i+2)
#define BUCKET_INIT_8(b, i)   BUCKET_INIT_4(b, i),   BUCKET_INIT_4(b, i+4)
#define BUCKET_INIT_16(b, i)  BUCKET_INIT_8(b, i),   BUCKET_INIT_8(b, i+8)
#define BUCKET_INIT_32(b, i)  BUCKET_INIT_16(b, i),  BUCKET_INIT_16(b, i+16)
#define BUCKET_INIT_64(b, i)  BUCKET_INIT_32(b, i),  BUCKET_INIT_32(b, i+32)
#define BUCKET_INIT_128(b, i) BUCKET_INIT_64(b, i),  BUCKET_INIT_64(b, i+64)
#define BUCKET_INIT_256(b, i) BUCKET_INIT_128(b, i), BUCKET_INIT_128(b, i+128)

// Table of waiters (hashed by address)
static Bucket buckets[NUM_BUCKETS] = {
    BUCKET_INIT_256(buckets, 0),
    BUCKET_INIT(buckets, 256),
};

static void enqueue(Bucket *bucket, const void *address, waiter *wait) {
    llist_insert_tail(&bucket->root, &wait->node);
    ++bucket->num_waiters;
}

static waiter * dequeue(Bucket *bucket, const void *address) {
    // find the first waiter that is waiting on `address`
    struct llist_node *root = &bucket->root;
    struct llist_node *node;
    llist_for_each(node, root) {
        waiter *wait = llist_data(node, waiter, node);
        if (wait->addr == (uintptr_t)address) {
            llist_remove(node);
            --bucket->num_waiters;
            return wait;
        }
    }
    return NULL;
}

static void dequeue_all(Bucket *bucket, const void *address, struct llist_node *dst) {
    // remove and append all matching waiters to dst
    struct llist_node *root = &bucket->root;
    struct llist_node *node;
    llist_for_each_safe(node, root) {
        waiter *wait = llist_data(node, waiter, node);
        if (wait->addr == (uintptr_t)address) {
            llist_remove(node);
            llist_insert_tail(dst, node);
            --bucket->num_waiters;
        }
    }
}

static void insert_waiter(void* addr, waiter* wait) {
    Bucket *bucket = &buckets[(uintptr_t)addr % NUM_BUCKETS];
    enqueue(bucket, addr, wait);
}

static void notify_all_waiters(void* addr) {
    struct llist_node head = LLIST_INIT(head);
    Bucket *bucket = &buckets[((uintptr_t)addr) % NUM_BUCKETS];
    dequeue_all(bucket, addr, &head);

    struct llist_node *node;
    llist_for_each_safe(node, &head) {
        waiter *wait = llist_data(node, waiter, node);
        llist_remove(node);
        wait->tstate->scheduler_state = _PyScheduler_STATE_RUNNABLE;
    }
}

static void _PyScheduler_AssertOk(_PyScheduler* scheduler) {
    assert(scheduler != NULL);
    assert(scheduler->initialized == 1);
    assert(PyGILState_Check() != 0);
}

void _PyScheduler_Init(_PyScheduler* scheduler, PyInterpreterState* interp, uint32_t seed) {
    assert(scheduler != NULL);

    scheduler->next = NULL;
    scheduler->interp = interp;
    init_genrand(&scheduler->random_obj, seed);
    scheduler->initialized = 1;
}

/* Invoke the scheduler to make a decision about the next thread to schedule.
   The actual scheduling takes place at GIL handoff. */
void _PyScheduler_Invoke(_PyScheduler* scheduler) {
    _PyScheduler_AssertOk(scheduler);

#ifdef Py_DEBUG
    fprintf(stderr, "[Scheduler] Invoking scheduler\n");
#endif

    int rng = genrand_uint32(&scheduler->random_obj) % scheduler->thread_count;
    int count = rng;

    HEAD_LOCK(&_PyRuntime);

    while (rng >= 0) {
        _Py_FOR_EACH_TSTATE_UNLOCKED(scheduler->interp, t) {
            if (t->scheduler_state == _PyScheduler_STATE_RUNNABLE) {
                if (rng == 0) {
#ifdef Py_DEBUG
                    fprintf(stderr, "[Scheduler] Thread %lld chosen as next thread",
                            PyThreadState_GetID(t));
#endif
                    _Py_atomic_store_ptr(&scheduler->next, t);
                }
                rng--;
            }
        }

        if (count == rng) {
            HEAD_UNLOCK(&_PyRuntime);
            Py_FatalError("No threads runnable when scheduler is invoked");
        }
    }

    HEAD_UNLOCK(&_PyRuntime);
}

/* This function doesn't need to do much since Python already does a lot of bookkeeping 
   when a new thread is created.

   ThreadHandles are tracked in `_PyRuntime._pythread_runtime_state` and new thread states are
   added in `interp->threads` before this function is called. */
void _PyScheduler_Instrument_AfterThreadStart(_PyScheduler* scheduler) {
    _PyScheduler_AssertOk(scheduler);

    _PyScheduler_Invoke(scheduler);
}

void _PyScheduler_Instrument_AfterThreadFinish(_PyScheduler* scheduler,
                                               ThreadHandle* thread_handle) {
    _PyScheduler_AssertOk(scheduler);

    notify_all_waiters(&thread_handle->has_exited);
    thread_handle->has_exited = 1;

    _PyScheduler_Invoke(scheduler);
}

/* Check if thread is joinable before actual join.
    If not joinable, remove the thread from the ready queue and release the GIL. */
void _PyScheduler_Instrument_BeforeThreadJoin(_PyScheduler* scheduler,
                                               ThreadHandle* thread_handle) {
    _PyScheduler_AssertOk(scheduler);

    PyThreadState* tstate = _PyThreadState_GET();
    assert(tstate != NULL);

    if (!thread_handle->has_exited) {
        /* Insert into waiting queue */
        waiter wait = {
            .addr = (uintptr_t)&thread_handle->has_exited,
            .tstate = tstate,
        };
        insert_waiter(&thread_handle->has_exited, &wait);

        /* Change scheduler state */
        tstate->scheduler_state = _PyScheduler_STATE_BLOCKED_THREAD_JOIN;
        _PyScheduler_Invoke(scheduler);

        /* Relinquish GIL */
        Py_BEGIN_ALLOW_THREADS
        Py_END_ALLOW_THREADS

        // When thread reaches here, it means that another thread has removed it from queue
        // and allowed it to take the GIL again
    }

    // _PyScheduler_ThreadHandle* th = (_PyScheduler_ThreadHandle*)thread_handle;

    // HEAD_LOCK(&_PyRuntime);

    // struct llist_node* head = &_PyRuntime.threads.handles;
    // struct llist_node* node;
    // llist_for_each(node, head) {
    //     _PyScheduler_ThreadHandle* h = llist_data(node, _PyScheduler_ThreadHandle, node);
    //     // Find the correct thread handle
    //     if (h->ident == th->ident) {
    //         // Check if thread has joined

    //         // No need to decref since it is done for us when the Python object is deleted
    //     }
    // }

    // if (!_PyEvent_IsSet(h->thread_is_exiting)) {
    //     tstate->scheduler_state = _PyScheduler_STATE_BLOCKED_THREAD_JOIN;
    //     _PyScheduler_Invoke(scheduler);
    //     Py_BEGIN_ALLOW_THREADS
    //     Py_END_ALLOW_THREADS
    // }

    // HEAD_UNLOCK(&_PyRuntime);
}
