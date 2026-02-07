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
    /* Linked list of `wait_entry` in this bucket */
    struct llist_node root;
    size_t num_waiters;
} Bucket;

typedef struct {
    /* Address of item this wait_entry is waiting on */
    uintptr_t addr;
    /* Represents the waiting thread */
    PyThreadState* tstate;
    struct llist_node node;
} wait_entry;

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

static void enqueue(Bucket *bucket, wait_entry *wait) {
    llist_insert_tail(&bucket->root, &wait->node);
    ++bucket->num_waiters;
}

static wait_entry* dequeue(Bucket *bucket, const void *address) {
    // find the first wait_entry that is waiting on `address`
    struct llist_node *root = &bucket->root;
    struct llist_node *node;
    llist_for_each(node, root) {
        wait_entry *wait = llist_data(node, wait_entry, node);
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
        wait_entry *wait = llist_data(node, wait_entry, node);
        if (wait->addr == (uintptr_t)address) {
            llist_remove(node);
            llist_insert_tail(dst, node);
            --bucket->num_waiters;
        }
    }
}

static void insert_waiter(void* addr, wait_entry* wait) {
    Bucket *bucket = &buckets[(uintptr_t)addr % NUM_BUCKETS];
    enqueue(bucket, wait);
}

static void notify_one_waiter(void* addr) {
    Bucket *bucket = &buckets[((uintptr_t)addr) % NUM_BUCKETS];

    // Find the first wait_entry that is waiting on `addr`
    wait_entry* wait = dequeue(bucket, addr);
    if (wait) {
        wait->tstate->scheduler_state = _PyScheduler_STATE_RUNNABLE;
    }
}

static void notify_all_waiters(void* addr) {
    struct llist_node head = LLIST_INIT(head);
    Bucket *bucket = &buckets[((uintptr_t)addr) % NUM_BUCKETS];
    dequeue_all(bucket, addr, &head);

    struct llist_node *node;
    llist_for_each_safe(node, &head) {
        wait_entry *wait = llist_data(node, wait_entry, node);
        llist_remove(node);
        wait->tstate->scheduler_state = _PyScheduler_STATE_RUNNABLE;
    }
}

#ifndef NDEBUG
static int _PyScheduler_CheckConsistency(_PyScheduler* scheduler) {
    assert(scheduler != NULL);
    assert(scheduler->initialized == 1);
    assert(PyGILState_Check() != 0);
    return 1;
}
#endif

void _PyScheduler_Init(_PyScheduler* scheduler, PyInterpreterState* interp, uint32_t seed) {
    assert(scheduler != NULL);

    scheduler->next = NULL;
    scheduler->interp = interp;
    scheduler->thread_count = 1;
    init_genrand(&scheduler->random_obj, seed);
    scheduler->initialized = 1;
}

/* Invoke the scheduler to make a decision about the next thread to schedule.
   The actual scheduling takes place at GIL handoff.
   
   `tstate_avoid` is used for the thread exit event to prevent the exiting thread from being
   scheduled, as the thread metadata is not yet removed from python runtime structs. */
void _PyScheduler_Invoke(_PyScheduler* scheduler) {
    assert(_PyScheduler_CheckConsistency(scheduler));

    HEAD_LOCK(&_PyRuntime);

#ifdef Py_DEBUG
    fprintf(stderr, "[Scheduler] Invoking scheduler\n");
    fprintf(stderr, "[Scheduler] Thread count: %d\n", scheduler->thread_count);
    fprintf(stderr, "[Scheduler] Thread states: ");
    _Py_FOR_EACH_TSTATE_UNLOCKED(scheduler->interp, t) {
        fprintf(stderr, "(%lld, %d), ", PyThreadState_GetID(t), t->scheduler_state);
    }
    fprintf(stderr, "\n");
#endif

    int runnable_count = 0;
    _Py_FOR_EACH_TSTATE_UNLOCKED(scheduler->interp, t) {
        if (t->scheduler_state == _PyScheduler_STATE_RUNNABLE) {
            runnable_count++;
        }
    }

    if (runnable_count == 0) {
        HEAD_UNLOCK(&_PyRuntime);
        Py_FatalError("There are no runnable threads runnable when the scheduler is invoked");
    }

    uint32_t idx = genrand_uint32(&scheduler->random_obj) % runnable_count;

    _Py_FOR_EACH_TSTATE_UNLOCKED(scheduler->interp, t) {
        if (t->scheduler_state == _PyScheduler_STATE_RUNNABLE) {
            if (idx == 0) {
#ifdef Py_DEBUG
                fprintf(stderr, "[Scheduler] Thread %llu chosen as next thread\n",
                        PyThreadState_GetID(t));
#endif
                _Py_atomic_store_ptr(&scheduler->next, t);
                break;
            }
            idx--;
        }
    }

    HEAD_UNLOCK(&_PyRuntime);
}

/* This function doesn't need to do much since Python already does a lot of bookkeeping 
   when a new thread is created.

   ThreadHandles are tracked in `_PyRuntime._pythread_runtime_state` and new thread states are
   added in `interp->threads` before this function is called. */
void _PyScheduler_ThreadStart(_PyScheduler* scheduler) {
    assert(_PyScheduler_CheckConsistency(scheduler));

#ifdef Py_DEBUG
    PyThreadState* tstate = PyThreadState_Get();
    fprintf(stderr, "[Scheduler] Thread %llu _PyScheduler_ThreadStart\n",
           PyThreadState_GetID(tstate));
#endif

    scheduler->thread_count++;

    _PyScheduler_Invoke(scheduler);

    /* Relinquish GIL */
    Py_BEGIN_ALLOW_THREADS
    Py_END_ALLOW_THREADS
}

void _PyScheduler_ThreadExit(_PyScheduler* scheduler, _PyScheduler_ThreadHandle* thread_handle) {
    assert(_PyScheduler_CheckConsistency(scheduler));

    PyThreadState* tstate = PyThreadState_Get();

#ifdef Py_DEBUG
    fprintf(stderr, "[Scheduler] Thread %llu _PyScheduler_ThreadExit\n",
            PyThreadState_GetID(tstate));
#endif

    notify_all_waiters(thread_handle);
    thread_handle->exited = 1;
    scheduler->thread_count--;
    tstate->scheduler_state = _PyScheduler_STATE_THREAD_EXITING;
    _PyScheduler_Invoke(scheduler);

    /* GIL is relinquished automatically in `thread_run` */
}

/* Check if thread is joinable before actual join.
    If not joinable, remove the thread from the ready queue and release the GIL. */
void _PyScheduler_ThreadJoin(_PyScheduler* scheduler, _PyScheduler_ThreadHandle* thread_handle) {
    assert(_PyScheduler_CheckConsistency(scheduler));

    PyThreadState* tstate = PyThreadState_Get();

#ifdef Py_DEBUG
    fprintf(stderr, "[Scheduler] Thread %llu _PyScheduler_ThreadJoin\n",
            PyThreadState_GetID(tstate));
#endif

    if (!thread_handle->exited) {
        wait_entry wait = {
            .addr = (uintptr_t)thread_handle,
            .tstate = tstate,
        };
        insert_waiter(thread_handle, &wait);

        tstate->scheduler_state = _PyScheduler_STATE_BLOCKED_THREAD_JOIN;
        _PyScheduler_Invoke(scheduler);
    }
    /* Relinquish GIL */
    Py_BEGIN_ALLOW_THREADS
    Py_END_ALLOW_THREADS
}

void _PyScheduler_lockobject_Acquire(_PyScheduler* scheduler,
                                     _PyScheduler_lockobject* lock,
                                     int blocking) {
    assert(_PyScheduler_CheckConsistency(scheduler));

    PyThreadState* tstate = PyThreadState_Get();

#ifdef Py_DEBUG
    fprintf(stderr, "[Scheduler] Thread %llu _PyScheduler_lockobject_Acquire\n",
            PyThreadState_GetID(tstate));
#endif

    while (1) {
        if (lock->locked == 0) {
            lock->locked = 1;
            _PyScheduler_Invoke(scheduler);
            break;
        } else if (!blocking) {
            break;
        } else {
            wait_entry wait = {
                .addr = (uintptr_t)lock,
                .tstate = tstate,
            };
            insert_waiter(lock, &wait);

            tstate->scheduler_state = _PyScheduler_STATE_BLOCKED_LOCK_ACQUIRE;
            _PyScheduler_Invoke(scheduler);

            /* Relinquish GIL */
            Py_BEGIN_ALLOW_THREADS
            Py_END_ALLOW_THREADS 
        }
    }
    /* Relinquish GIL */
    Py_BEGIN_ALLOW_THREADS
    Py_END_ALLOW_THREADS
}

void _PyScheduler_lockobject_Release(_PyScheduler* scheduler, _PyScheduler_lockobject* lock) {
    assert(_PyScheduler_CheckConsistency(scheduler));

#ifdef Py_DEBUG
    PyThreadState* tstate = PyThreadState_Get();
    fprintf(stderr, "[Scheduler] Thread %llu _PyScheduler_lockobject_Release\n",
            PyThreadState_GetID(tstate));
#endif

    notify_one_waiter(lock);
    lock->locked = 0;
    _PyScheduler_Invoke(scheduler);

    /* Relinquish GIL */
    Py_BEGIN_ALLOW_THREADS
    Py_END_ALLOW_THREADS
}

void _PyScheduler_rlockobject_Acquire(_PyScheduler* scheduler,
                                      _PyScheduler_rlockobject* lock,
                                      int blocking) {
    assert(_PyScheduler_CheckConsistency(scheduler));

    PyThreadState* tstate = PyThreadState_Get();

#ifdef Py_DEBUG
    fprintf(stderr, "[Scheduler] Thread %llu _PyScheduler_rlockobject_Acquire\n",
            PyThreadState_GetID(tstate));
#endif

    while (1) {
        if (lock->owner == tstate) {
            lock->level++;
            _PyScheduler_Invoke(scheduler);
            break;
        } else if (lock->locked == 0) {
            lock->level = 0;
            lock->locked = 1;
            lock->owner = tstate;
            _PyScheduler_Invoke(scheduler);
            break;
        } else if (!blocking) {
            break;
        } else {
            wait_entry wait = {
                .addr = (uintptr_t)lock,
                .tstate = tstate,
            };
            insert_waiter(lock, &wait);

            tstate->scheduler_state = _PyScheduler_STATE_BLOCKED_RLOCK_ACQUIRE;
            _PyScheduler_Invoke(scheduler);

            /* Relinquish GIL */
            Py_BEGIN_ALLOW_THREADS
            Py_END_ALLOW_THREADS 
        }
    }
    /* Relinquish GIL */
    Py_BEGIN_ALLOW_THREADS
    Py_END_ALLOW_THREADS
}

void _PyScheduler_rlockobject_Release(_PyScheduler* scheduler, _PyScheduler_rlockobject* lock) {
    assert(_PyScheduler_CheckConsistency(scheduler));
    assert(lock->owner == PyThreadState_Get());

    if (lock->level > 0) {
        lock->level--;
    } else {
        notify_one_waiter(lock);
        lock->locked = 0;
        lock->owner = NULL;
    }

    _PyScheduler_Invoke(scheduler);

    /* Relinquish GIL */
    Py_BEGIN_ALLOW_THREADS
    Py_END_ALLOW_THREADS
}
