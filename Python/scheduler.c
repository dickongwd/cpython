#include "Python.h"
#include "pycore_scheduler.h"

void _PyScheduler_Init(_PyScheduler* scheduler) {
    scheduler->next = NULL;
    scheduler->initialized = 1;
}
