#include "Python.h"
#include "pycore_scheduler.h"
#include "pycore_interp_structs.h"

void _PyScheduler_Init(_PyScheduler* scheduler, PyInterpreterState* interp) {
    scheduler->interp = interp;
    scheduler->counter = 0;
    scheduler->initialized = 1;
}

int _PyScheduler_GetNext(_PyScheduler* scheduler) {
    assert(scheduler != NULL);
    assert(scheduler->initialized == 1);

    PyInterpreterState* interp = scheduler->interp;
    if (interp->threads.count == 0) {
        return -1;
    }

    scheduler->counter = (scheduler->counter + 1) % interp->threads.count;
    return scheduler->counter;
}
