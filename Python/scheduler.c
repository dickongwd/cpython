#include "Python.h"
#include "pycore_interp_structs.h"
#include "pycore_pystate.h"
#include "pycore_runtime.h"
#include "pycore_scheduler.h"
#include "pystate.h"

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
        /* only MSBs of the array mt[].                                */
        /* 2002/01/09 modified by Makoto Matsumoto                     */
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

void _PyScheduler_Init(_PyScheduler* scheduler, PyInterpreterState* interp, uint32_t seed) {
    scheduler->next = NULL;
    scheduler->interp = interp;
    init_genrand(&scheduler->random_obj, seed);
    scheduler->initialized = 1;
}

/* Sets the next thread to be scheduled.
   This has to be called with the GIL locked since it traverses the 
   list of threads in interpreter state. */
void _PyScheduler_SetNext(_PyScheduler* scheduler) {
    assert(scheduler != NULL);
    assert(scheduler->initialized == 1);

#ifdef Py_DEBUG
    fprintf(stderr, "[Scheduler] Determining the next thread\n");
#endif

    PyInterpreterState* interp = scheduler->interp;

    // TODO modulo number of threads
    unsigned int raw = genrand_uint32(&scheduler->random_obj);
    int rng = raw % 10;
    int num = rng;
#ifdef Py_DEBUG
    fprintf(stderr, "[RNG] Raw: %u\n", raw);
    fprintf(stderr, "[RNG] Num: %u\n", num);
#endif
    PyThreadState* ts = NULL;

    HEAD_LOCK(&_PyRuntime);

    while (num >= 0) {
        // Main thread
        ts = interp->runtime->main_tstate;
        if (ts->scheduler_state == SCHEDULER_STATE_RUNNABLE) {
            if (num == 0) {
#ifdef Py_DEBUG
                fprintf(stderr, "[Scheduler] Thread %lld is chosen as next thread\n", PyThreadState_GetID(ts));
#endif
                _Py_atomic_store_ptr(&scheduler->next, ts);
            }
            num--;
        }

        for (ts = PyInterpreterState_ThreadHead(interp); ts != NULL && num >= 0; ts = PyThreadState_Next(ts)) {
            if (ts->scheduler_state == SCHEDULER_STATE_RUNNABLE) {
                if (num == 0) {
#ifdef Py_DEBUG
                    fprintf(stderr, "[Scheduler] Thread %lld is chosen as next thread\n", PyThreadState_GetID(ts));
#endif
                    _Py_atomic_store_ptr(&scheduler->next, ts);
                }
                num--;
            };
        }

        // No threads available
        if (num == rng) {
#ifdef Py_DEBUG
            fprintf(stderr, "[Scheduler] Next chosen thread set as NULL\n");
#endif
            _Py_atomic_store_ptr(&scheduler->next, NULL);
            break;
        }
    }

    HEAD_UNLOCK(&_PyRuntime);
}

/* Iterates through all threads and sets thread states which are waiting
   on the specified event (addr) to be runnable.
   Has to called with the GIL held. 
   This could probably be more efficient than scanning through all threads. */
void _PyScheduler_Notify(_PyScheduler* scheduler, uintptr_t addr) {
#ifdef Py_DEBUG
    fprintf(stderr, "[Thread %lld]", PyThreadState_GetID(PyThreadState_Get()));
    fprintf(stderr, "[Scheduler] Notifying threads\n");
#endif

    PyInterpreterState* interp = scheduler->interp;

    HEAD_LOCK(&_PyRuntime);

    // Main thread
    PyThreadState* ts = interp->runtime->main_tstate;
    if (ts->waiting_event == addr) {
#ifdef Py_DEBUG
        fprintf(stderr, "[Thread %lld]", PyThreadState_GetID(PyThreadState_Get()));
        fprintf(stderr, "[Scheduler] Notified thread %lld and made it ready\n", PyThreadState_GetID(ts));
#endif
        ts->scheduler_state = SCHEDULER_STATE_RUNNABLE;
        ts->waiting_event = 0;
    }

    // All other threads in interpreter
    for (ts = PyInterpreterState_ThreadHead(interp); ts != NULL; ts = PyThreadState_Next(ts)) {
        if (ts->waiting_event == addr) {
#ifdef Py_DEBUG
            fprintf(stderr, "[Thread %lld]", PyThreadState_GetID(PyThreadState_Get()));
            fprintf(stderr, "[Scheduler] Notified thread %lld and made it ready\n", PyThreadState_GetID(ts));
#endif
            ts->scheduler_state = SCHEDULER_STATE_RUNNABLE;
            ts->waiting_event = 0;
        }
    }

    HEAD_UNLOCK(&_PyRuntime);

    _PyScheduler_SetNext(scheduler);
}

void _PyScheduler_SetWaitingEvent(_PyScheduler* scheduler, PyThreadState* ts, uintptr_t event, int new_state) {
#ifdef Py_DEBUG
    fprintf(stderr, "[Thread %lld]", PyThreadState_GetID(PyThreadState_Get()));
    fprintf(stderr, "[Scheduler] State set to %d\n", new_state);
#endif
    ts->scheduler_state = new_state;
    ts->waiting_event = event;

    // Reset the next thread since a thread's state has changed
    _PyScheduler_SetNext(scheduler);
}
