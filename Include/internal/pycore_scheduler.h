#ifndef _Py_SCHEDULER_H
#define _Py_SCHEDULER_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef Py_BUILD_CORE
#  error "this header requires Py_BUILD_CORE define"
#endif

#include "pytypedefs.h"

/* Copied/referenced over from _randommodule.c */
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

#ifdef __cplusplus
}
#endif
#endif  // !_Py_SCHEDULER_H
