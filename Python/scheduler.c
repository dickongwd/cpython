#include "Python.h"
#include "pycore_interp_structs.h"
#include "pycore_pystate.h"
#include "pycore_runtime.h"
#include "pycore_scheduler.h"
#include "pystate.h"

void _PyScheduler_Init(_PyScheduler* scheduler, PyInterpreterState* interp) {
    scheduler->next = NULL;
    scheduler->interp = interp;
    scheduler->initialized = 1;
}

/* Sets the next thread to be scheduled.
   This has to be called with the GIL locked since it traverses the 
   list of threads in interpreter state. */
void _PyScheduler_SetNext(_PyScheduler* scheduler) {
    assert(scheduler != NULL);
    assert(scheduler->initialized == 1);

    PyInterpreterState* interp = scheduler->interp;

    // TODO use random number
    int num = 0;
    PyThreadState* ts = NULL;

    HEAD_LOCK(&_PyRuntime);
    fprintf(stderr, "PYSCHEDULER SETTING\n");

    while (num >= 0) {
        // Main thread
        ts = interp->runtime->main_tstate;
        fprintf(stderr, "main thread state is %d\n", ts->scheduler_state);
        if (ts->scheduler_state == SCHEDULER_STATE_RUNNABLE) {
            if (num == 0) {
                fprintf(stderr, "chosen main thread\n");
                _Py_atomic_store_ptr(&scheduler->next, ts);
            }
            num--;
        }

        for (ts = PyInterpreterState_ThreadHead(interp); ts != NULL && num >= 0; ts = PyThreadState_Next(ts)) {
            if (ts->scheduler_state == SCHEDULER_STATE_RUNNABLE) {
                if (num == 0) {
                    fprintf(stderr, "chosen thread %d\n", PyThreadState_GetID(ts));
                    _Py_atomic_store_ptr(&scheduler->next, ts);
                }
                num--;
            };
        }

        // TODO fix
        if (num == 0) {
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

    PyInterpreterState* interp = scheduler->interp;

    HEAD_LOCK(&_PyRuntime);

    // Main thread
    PyThreadState* ts = interp->runtime->main_tstate;
    if (ts->waiting_event == addr) {
        ts->scheduler_state = SCHEDULER_STATE_RUNNABLE;
        ts->waiting_event = 0;
    }

    // All other threads in interpreter
    for (ts = PyInterpreterState_ThreadHead(interp); ts != NULL; ts = PyThreadState_Next(ts)) {
        if (ts->waiting_event == addr) {
            ts->scheduler_state = SCHEDULER_STATE_RUNNABLE;
            ts->waiting_event = 0;
        }
    }

    HEAD_UNLOCK(&_PyRuntime);

    _PyScheduler_SetNext(scheduler);
}
