#ifndef Py_INTERNAL_SCHEDULER_H
#define Py_INTERNAL_SCHEDULER_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef Py_BUILD_CORE
#  error "this header requires Py_BUILD_CORE define"
#endif

#include "pycore_llist.h"
#include "pytypedefs.h"
#include "threadhandle.h"

#define _PyScheduler_STATE_RUNNABLE 0
#define _PyScheduler_STATE_BLOCKED_THREAD_JOIN 1
#define _PyScheduler_STATE_BLOCKED_LOCK_ACQUIRE 2

/* Copied/referenced over from `_randommodule.c` */
typedef struct {
    int index;
    uint32_t state[624];
} _PyScheduler_RandomObject;

typedef struct {

    /* Points to the next thread state to schedule.
       Needs to be read from and written to atomically. */
    PyThreadState* next;

    /* Backward reference */
    PyInterpreterState* interp;

    /* Number of threads.

       interp->threads.count is not used as it is used by users of the threading module, and is
       incremented in the new thread when some key operations have already taken place (e.g.
       acquire GIL).

       Protected by HEAD_LOCK(runtime). */
    Py_ssize_t thread_count;

    /* Used for rng */
    _PyScheduler_RandomObject random_obj;

    int initialized;

} _PyScheduler;

extern void _PyScheduler_Init(_PyScheduler* scheduler, PyInterpreterState* interp, uint32_t seed);
extern void _PyScheduler_Invoke(_PyScheduler* scheduler);

/* Thread creation and join */
extern void _PyScheduler_Instrument_AfterThreadStart(_PyScheduler* scheduler);
extern void _PyScheduler_Instrument_AfterThreadFinish(_PyScheduler* scheduler, ThreadHandle* thread_handle);
extern void _PyScheduler_Instrument_BeforeThreadJoin(_PyScheduler* scheduler, ThreadHandle* thread_handle);

#ifdef __cplusplus
}
#endif
#endif  // !Py_SCHEDULER_H
