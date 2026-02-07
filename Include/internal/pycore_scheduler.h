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

#define _PyScheduler_STATE_RUNNABLE 0
#define _PyScheduler_STATE_THREAD_EXITING 1
#define _PyScheduler_STATE_BLOCKED_THREAD_JOIN 2
#define _PyScheduler_STATE_BLOCKED_LOCK_ACQUIRE 3
#define _PyScheduler_STATE_BLOCKED_RLOCK_ACQUIRE 4

/* Copied/referenced over from `_randommodule.c` */
typedef struct {
    int index;
    uint32_t state[624];
} _PyScheduler_RandomObject;

typedef struct {
    int exited;
} _PyScheduler_ThreadHandle;

/* For instrumenting `lockobject` in `_threadmodule.c` */
typedef struct {
    int locked;
} _PyScheduler_lockobject;

/* For instrumenting `rlockobject` in `_threadmodule.c` */
typedef struct {
    int locked;
    int level;
    PyThreadState* owner;
} _PyScheduler_rlockobject;

typedef struct {
    /* Points to the next thread state to schedule.
       Needs to be read from and written to atomically. */
    PyThreadState* next;

    /* Backward reference */
    PyInterpreterState* interp;

    /* Number of threads.

       interp->threads.count is not used as it is used by users of the threading module, and is
       incremented in the new thread when some key operations have already taken place (e.g.
       acquire GIL). */
    Py_ssize_t thread_count;

    /* Used for rng */
    _PyScheduler_RandomObject random_obj;

    int initialized;
} _PyScheduler;

extern void _PyScheduler_Init(_PyScheduler* scheduler, PyInterpreterState* interp, uint32_t seed);
extern void _PyScheduler_Invoke(_PyScheduler* scheduler);

/* Thread creation, exit and join */
extern void _PyScheduler_ThreadStart(_PyScheduler* scheduler);
extern void _PyScheduler_ThreadExit(_PyScheduler* scheduler,
                                    _PyScheduler_ThreadHandle* thread_handle);
extern void _PyScheduler_ThreadJoin(_PyScheduler* scheduler,
                                    _PyScheduler_ThreadHandle* thread_handle);

/* `lockobject` in `_threadmodule.c` */
extern void _PyScheduler_lockobject_Acquire(_PyScheduler* scheduler,
                                            _PyScheduler_lockobject* lock,
                                            int blocking);
extern void _PyScheduler_lockobject_Release(_PyScheduler* scheduler, _PyScheduler_lockobject* lock);

/* `rlockobject` in `_threadmodule.c` */
extern void _PyScheduler_rlockobject_Acquire(_PyScheduler* scheduler,
                                             _PyScheduler_rlockobject* lock,
                                             int blocking);
extern void _PyScheduler_rlockobject_Release(_PyScheduler* scheduler, _PyScheduler_rlockobject* lock);

#ifdef __cplusplus
}
#endif

#endif  // !Py_INTERNAL_SCHEDULER_H
