#ifndef Py_INTERNAL_THREADHANDLE_H
#define Py_INTERNAL_THREADHANDLE_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef Py_BUILD_CORE
#  error "this header requires Py_BUILD_CORE define"
#endif

#include "Python.h"
#include "pycore_llist.h"
#include "pycore_lock.h"
#include "pycore_pythread.h"


/* Moved from `_threadmodule.c` */

// A handle to wait for thread completion.
//
// This may be used to wait for threads that were spawned by the threading
// module as well as for the "main" thread of the threading module. In the
// former case an OS thread, identified by the `os_handle` field, will be
// associated with the handle. The handle "owns" this thread and ensures that
// the thread is either joined or detached after the handle is destroyed.
//
// Joining the handle is idempotent; the underlying OS thread, if any, is
// joined or detached only once. Concurrent join operations are serialized
// until it is their turn to execute or an earlier operation completes
// successfully. Once a join has completed successfully all future joins
// complete immediately.
//
// This must be separately reference counted because it may be destroyed
// in `thread_run()` after the PyThreadState has been destroyed.
typedef struct {
    struct llist_node node;  // linked list node (see _pythread_runtime_state)

    // linked list node (see thread_module_state)
    struct llist_node shutdown_node;

    // The `ident`, `os_handle`, `has_os_handle`, and `state` fields are
    // protected by `mutex`.
    PyThread_ident_t ident;
    PyThread_handle_t os_handle;
    int has_os_handle;

    // Holds a value from the `ThreadHandleState` enum.
    int state;

    PyMutex mutex;

    // Set immediately before `thread_run` returns to indicate that the OS
    // thread is about to exit. This is used to avoid false positives when
    // detecting self-join attempts. See the comment in `ThreadHandle_join()`
    // for a more detailed explanation.
    PyEvent thread_is_exiting;

    // Same as `thread_is_exiting`, but used for the scheduler. This separate event is created
    // Because the `thread_is_exiting` event can be notified without the GIL.
    int has_exited;

    // Serializes calls to `join` and `set_done`.
    _PyOnceFlag once;

    Py_ssize_t refcount;
} ThreadHandle;

#ifdef __cplusplus
}
#endif

#endif      // !Py_INTERNAL_THREADHANDLE_H
