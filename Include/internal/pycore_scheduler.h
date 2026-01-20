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
#define SCHEDULER_STATE_BLOCKED 1
#define SCHEDULER_STATE_BLOCKED_SYNC 2

typedef struct {
    /* Points to the next thread to schedule. 
       Needs to be read from and written to atomically. */
    PyThreadState* next;

    /* Backward reference to interpreter state */
    PyInterpreterState* interp;

    int initialized;

    /* List of `thread_is_exiting` events. */
    PyEvent* thread_exit_events;

    //

    // PyObject* random_module;
    // PyObject* random_instance;
} _PyScheduler;

extern void _PyScheduler_Init(_PyScheduler* scheduler, PyInterpreterState* interp);
extern void _PyScheduler_SetNext(_PyScheduler* scheduler);

extern void _PyScheduler_Notify(_PyScheduler* scheduler, PyEvent* event);


#ifdef __cplusplus
}
#endif
#endif   // !_Py_SCHEDULER_H
