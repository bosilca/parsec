/*
 * Copyright (c) 2009-2016 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 */

#ifndef _PARSEC_scheduling_h
#define _PARSEC_scheduling_h

#include "parsec_internal.h"

BEGIN_C_DECLS

/**
 * Mark a execution context as being ready to be scheduled, i.e. all
 * input dependencies are resolved. The execution context can be
 * executed immediately or delayed until resources become available.
 *
 * @param [IN] The execution unit where the tasks are to be proposed
 *             for scheduling. This is a hint, as the scheduling engine
 *             is free to push them where it decides.
 * @param [IN] The execution context to be executed. This include
 *             calling the attached hook (if any) as well as marking
 *             all dependencies as completed.
 * @param [IN] Suggested distance to the current state where the tasks
 *             are to be pushed. The larger the value (in absolute) the
 *             further away the tasks will be pushed. This is a hint
 *             that the schedulers are free to ignore.
 *
 * @return  0 If the execution was succesful and all output dependencies
 *            has been correctly marked.
 * @return -1 If something went wrong.
 */
int __parsec_schedule( parsec_execution_unit_t*,
                       parsec_execution_context_t*,
                       int32_t distance);

int __parsec_context_wait(parsec_execution_unit_t* eu_context);

/**
 * Execute the body of the task associated to the context.
 */
int __parsec_execute( parsec_execution_unit_t*, parsec_execution_context_t*);
/**
 * Signal the termination of the execution context to all dependencies of
 * its dependencies.
 *
 * @param [IN]  The execution context of the finished task.
 * @param [IN]  The task to be completed
 *
 * @return 0    If the dependencies have successfully been signaled.
 * @return -1   If something went wrong.
 */
int __parsec_complete_execution( parsec_execution_unit_t *eu_context,
                                 parsec_execution_context_t *exec_context );

/**
 * Signal the handle that a certain number of runtime bound activities have been
 * completed. Such activities includes network communications, other local data
 * transfers, and more generally any activity generated by the runtime itself
 * that is related to the handle. In addition, we assume that one extra activity
 * is to the capability of the upper level to generate tasks, activity that has
 * it's own counter, handled via parsec_handle_update_nbtask. Thus, once the
 * upper level knows no local tasks will be further generate it is it's
 * responsability to update the runtime counter accordingly.
 *
 * @return 0 if the handle has not been completed.
 * @return 1 if the handle has been completed and it has been marked for release.
 */
int parsec_handle_update_runtime_nbtask(parsec_handle_t *parsec_handle, int32_t nb_tasks);

/**
 * When changing the number of local tasks, see if we need to call the
 * DAG complete_cb callback, and/or if we need to update the number of
 * active objects.
 *
 * remaining is the number of local tasks available, after updating it
 * using the appropriate atomic operation
 */
int parsec_check_complete_cb(parsec_handle_t *parsec_handle, parsec_context_t *context, int remaining);

/**
 * Loads the scheduler as selected using the MCA logic
 * You better not call this while computations are in progress,
 *  i.e. it should be safe to call this when the main thread is
 *  not yet inside parsec_progress, but *before* any call to
 *  parsec_progress...
 *
 *  @RETURN 1 if the new scheduler was succesfully installed
 *          0 if it failed. In this case, the previous scheduler
 *            is kept.
 */
int parsec_set_scheduler( parsec_context_t *parsec );

/**
 *  Removes the current scheduler (cleanup)
 */
void parsec_remove_scheduler( parsec_context_t *parsec );

struct parsec_sched_module_s;
extern struct parsec_sched_module_s *current_scheduler;

END_C_DECLS

#endif  /* _PARSEC_scheduling_h */
