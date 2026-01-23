#ifndef _Py_SCHEDULER_H
#define _Py_SCHEDULER_H
#ifdef __cplusplus
extern "C" {
#endif

#ifndef Py_BUILD_CORE
#  error "this header requires Py_BUILD_CORE define"
#endif

#include "pytypedefs.h"

#define SCHEDULER_STATE_RUNNABLE 0
#define SCHEDULER_STATE_BLOCKED_THREAD_JOIN 1
#define SCHEDULER_STATE_BLOCKED_MUTEX_LOCK 2

/* Copied/referenced over from _randommodule.c */
typedef struct {
    int index;
    uint32_t state[624];
} _PyScheduler_RandomObject;

typedef struct {
    /* Points to the next thread to schedule. 
       Needs to be read from and written to atomically. */
    PyThreadState* next;

    /* Backward reference to interpreter state */
    PyInterpreterState* interp;

    /* Used for rng */
    _PyScheduler_RandomObject random_obj;

    int initialized;

    // PyObject* random_module;
    // PyObject* random_instance;
} _PyScheduler;

extern void _PyScheduler_Init(_PyScheduler* scheduler, PyInterpreterState* interp, uint32_t seed);
extern void _PyScheduler_SetNext(_PyScheduler* scheduler);
extern void _PyScheduler_Notify(_PyScheduler* scheduler, uintptr_t event);
extern void _PyScheduler_SetWaitingEvent(_PyScheduler* scheduler, PyThreadState* ts, uintptr_t event, int new_state);


#ifdef __cplusplus
}
#endif
#endif   // !_Py_SCHEDULER_H
