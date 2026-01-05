#ifndef _Py_SCHEDULER_H
#define _Py_SCHEDULER_H
#ifdef __cplusplus
extern "C" {
#endif

#ifndef Py_BUILD_CORE
#  error "this header requires Py_BUILD_CORE define"
#endif

#include "pytypedefs.h" // PyInterpreterState, PyObject

typedef struct {
    /* Backward reference to interpreter state */
    PyInterpreterState* interp;

    int counter;
    int initialized;

    // PyObject* random_module;
    // PyObject* random_instance;
} _PyScheduler;

extern void _PyScheduler_Init(_PyScheduler* scheduler, PyInterpreterState* interp);
extern int _PyScheduler_GetNext(_PyScheduler* scheduler);


#ifdef __cplusplus
}
#endif
#endif   // !_Py_SCHEDULER_H
