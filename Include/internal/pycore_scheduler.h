#ifndef _Py_SCHEDULER_H
#define _Py_SCHEDULER_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef Py_BUILD_CORE
#  error "this header requires Py_BUILD_CORE define"
#endif

#include "pytypedefs.h"

typedef struct {

    /* Points to the next thread state to schedule.
       Needs to be read from and written to atomically. */
    PyThreadState* next;

    int initialized;

} _PyScheduler;

extern void _PyScheduler_Init(_PyScheduler* scheduler);

#ifdef __cplusplus
}
#endif
#endif  // !_Py_SCHEDULER_H
