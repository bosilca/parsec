/*
 * Copyright (c) 2009      The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>

#include "dplasma.h"
#include "scheduling.h"
#include "dequeue.h"
#include "barrier.h"
#ifdef DPLASMA_PROFILING
#include "profiling.h"
#endif
#include "remote_dep.h"

static const dplasma_t** dplasma_array = NULL;
static int dplasma_array_size = 0, dplasma_array_count = 0;

void dplasma_dump(const dplasma_t *d, const char *prefix)
{
    char *pref2 = malloc(strlen(prefix)+3);
    int i;

    sprintf(pref2, "%s  ", prefix);
    printf("%sDplasma Function: %s\n", prefix, d->name);

    printf("%s Parameter Variables:\n", prefix);
    for(i = 0; i < d->nb_params; i++) {
        symbol_dump(d->params[i], pref2);
    }

    printf("%s Local Variables:\n", prefix);
    for(i = 0; i < MAX_LOCAL_COUNT && NULL != d->locals[i]; i++) {
        symbol_dump(d->locals[i], pref2);
    }

    printf("%s Predicates:\n", prefix);
    for(i = 0; i < MAX_PRED_COUNT && NULL != d->preds[i]; i++) {
        printf("%s", pref2);
        expr_dump(stdout, d->preds[i]);
        printf("\n");
    }

    printf("%s Parameters and Dependencies:\n", prefix);
    for(i = 0; i < MAX_PARAM_COUNT && NULL != d->inout[i]; i++) {
        param_dump(d->inout[i], pref2);
    }

    printf("%s Required dependencies mask: 0x%x (%s/%s/%s)\n", prefix,
           (int)d->dependencies_mask, (d->flags & DPLASMA_HAS_IN_IN_DEPENDENCIES ? "I" : "N"),
           (d->flags & DPLASMA_HAS_OUT_OUT_DEPENDENCIES ? "O" : "N"),
           (d->flags & DPLASMA_HAS_IN_STRONG_DEPENDENCIES ? "S" : "N"));
    printf("%s Body:\n", prefix);
    printf("%s  %s\n", prefix, d->body);

    if( NULL != d->deps ) {
        
        printf( "Current dependencies\n" );
    }

    free(pref2);
}

int dplasma_dplasma_index( const dplasma_t *d )
{
    int i;
    for(i = 0; i < dplasma_array_count; i++) {
        if( dplasma_array[i] == d ) {
            return i;
        }
    }
    return -1;
}

void dplasma_dump_all( void )
{
    int i;

    for( i = 0; i < dplasma_array_count; i++ ) {
        printf("/**\n * dplasma_t object named %s index %d\n */\n", dplasma_array[i]->name, i );
        dplasma_dump( dplasma_array[i], "" );
    }
}

int dplasma_push( const dplasma_t* d )
{
    if( dplasma_array_count >= dplasma_array_size ) {
        if( 0 == dplasma_array_size ) {
            dplasma_array_size = 4;
        } else {
            dplasma_array_size *= 2;
        }
        dplasma_array = (const dplasma_t**)realloc( dplasma_array, dplasma_array_size * sizeof(dplasma_t*) );
        if( NULL == dplasma_array ) {
            return -1;  /* No more available memory */
        }
    }
    dplasma_array[dplasma_array_count] = d;
    dplasma_array_count++;
    return 0;
}

const dplasma_t* dplasma_find( const char* name )
{
    int i;
    const dplasma_t* object;

    for( i = 0; i < dplasma_array_count; i++ ) {
        object = dplasma_array[i];
        if( 0 == strcmp( object->name, name ) ) {
            return object;
        }
    }
    return NULL;
}

dplasma_t* dplasma_find_or_create( const char* name )
{
    dplasma_t* object;

    object = (dplasma_t*)dplasma_find(name);
    if( NULL != object ) {
        return object;
    }
    object = (dplasma_t*)calloc(1, sizeof(dplasma_t));
    object->name = strdup(name);
    if( 0 == dplasma_push(object) ) {
        return object;
    }
    free(object);
    return NULL;
}

void dplasma_load_array( dplasma_t *array, int size )
{
    int i;

    dplasma_array_size = size;
    dplasma_array_count = size;
    dplasma_array = (const dplasma_t**)calloc(size, sizeof(dplasma_t*));
    for(i = 0; i < size; i++) {
        dplasma_array[i] = &(array[i]);
    }
}

const dplasma_t* dplasma_element_at( int i )
{
    if( i < dplasma_array_count ){
        return dplasma_array[i];
    }
    return NULL;
}

int dplasma_nb_elements( void )
{
    return dplasma_array_count;
}

/**
 *
 */
#ifdef DPLASMA_USE_GLOBAL_LIFO
extern dplasma_atomic_lifo_t ready_list;
#endif  /* DPLASMA_USE_GLOBAL_LIFO */

dplasma_context_t* dplasma_init( int nb_cores, int* pargc, char** pargv[] )
{
    dplasma_context_t* context = (dplasma_context_t*)malloc(sizeof(dplasma_context_t)+
                                                            nb_cores * sizeof(dplasma_execution_unit_t));
    int i;

    context->nb_cores = nb_cores;

    /* Initialize the barrier */
    dplasma_barrier_init( &(context->barrier), NULL, nb_cores );

#ifdef DPLASMA_GENERATE_DOT
    printf("digraph G {\n");
#endif  /* DPLASMA_GENERATE_DOT */
#ifdef DPLASMA_PROFILING
    dplasma_profiling_init( context, 4096 );
#endif  /* DPLASMA_PROFILING */

#if defined(DPLASMA_USE_GLOBAL_LIFO)
    dplasma_atomic_lifo_construct(&ready_list);
#endif  /* defined(DPLASMA_USE_GLOBAL_LIFO) */

    /* Prepare the LIFO task queue for each execution unit */
    for( i = 0; i < nb_cores; i++ ) {
        dplasma_execution_unit_t* eu = &(context->execution_units[i]);
#ifdef DPLASMA_USE_LIFO
        eu->eu_task_queue = (dplasma_atomic_lifo_t*)malloc( sizeof(dplasma_atomic_lifo_t) );
        dplasma_atomic_lifo_construct( eu->eu_task_queue );
#elif defined(DPLASMA_USE_GLOBAL_LIFO)
        /* Everybody share the same global LIFO */
        eu->eu_task_queue = &ready_list;
#else
        eu->eu_task_queue = (dplasma_dequeue_t*)malloc( sizeof(dplasma_dequeue_t) );
        dplasma_dequeue_construct( eu->eu_task_queue );
        eu->placeholder = NULL;
#endif  /* DPLASMA_USE_LIFO */
        context->execution_units[i].eu_id = i;
        context->execution_units[i].master_context = context;
    }

    if( nb_cores > 1 ) {
        pthread_attr_t thread_attr;

        pthread_attr_init(&thread_attr);
        pthread_attr_setscope(&thread_attr, PTHREAD_SCOPE_SYSTEM);
#ifdef __linux
        pthread_setconcurrency(nb_cores);
#endif  /* __linux */

        /* The first execution unit is for the master thread */
        for( i = 1; i < context->nb_cores; i++ ) {
            pthread_create( &((context)->execution_units[i].pthread_id),
                            &thread_attr,
                            (void* (*)(void*))__dplasma_progress,
                            (void*)&(context->execution_units[i]));
        }
    }

    dplasma_remote_dep_init(context);

    return context;
}

/**
 *
 */
int dplasma_fini( dplasma_context_t** context )
{
    int i;

#ifdef DPLASMA_GENERATE_DOT
    printf("}\n");
#endif  /* DPLASMA_GENERATE_DOT */
    
    dplasma_remote_dep_fini(*context);
    
#ifdef DPLASMA_PROFILING
    dplasma_profiling_fini( *context );
#endif  /* DPLASMA_PROFILING */

        /* The first execution unit is for the master thread */
    for(i = 1; i < (*context)->nb_cores; i++) {
        pthread_join( (*context)->execution_units[i].pthread_id, NULL );
#if defined(DPLASMA_USE_LIFO) || !defined(DPLASMA_USE_GLOBAL_LIFO)
        free( (*context)->execution_units[i].eu_task_queue );
        (*context)->execution_units[i].eu_task_queue = NULL;
#endif  /* defined(DPLASMA_USE_LIFO) || !defined(DPLASMA_USE_GLOBAL_LIFO) */
    }

    /* Destroy all resources allocated for the barrier */
    dplasma_barrier_destroy( &((*context)->barrier) );

    free(*context);
    *context = NULL;
    return 0;
}

/**
 * Compute the correct initial values for an execution context. These values
 * are in the range and validate all possible predicates. If such values do
 * not exist this function returns -1.
 */
int dplasma_set_initial_execution_context( dplasma_execution_context_t* exec_context )
{
    int i, rc;
    const dplasma_t* object = exec_context->function;
    const expr_t** predicates = (const expr_t**)object->preds;

    /* Compute the number of local values */
    if( 0 == object->nb_locals ) {
        /* special case for the IN/OUT objects */
        return 0;
    }

    for( i = 0; i < object->nb_locals; i++ ) {
        int min;
        exec_context->locals[i].sym = object->locals[i];
        rc = dplasma_symbol_get_first_value(object->locals[i], predicates,
                                            exec_context->locals, &min);
        if( rc != EXPR_SUCCESS ) {
        initial_values_one_loop_up:
            i--;
            if( i < 0 ) {
                printf( "Impossible to find initial values. Giving up\n" );
                return -1;
            }
            rc = dplasma_symbol_get_next_value(object->locals[i], predicates,
                                               exec_context->locals, &min );
            if( rc != EXPR_SUCCESS ) {
                goto initial_values_one_loop_up;
            }
        }
    }
    if( i < MAX_LOCAL_COUNT ) {
        exec_context->locals[i].sym = NULL;
    }
    return 0;
}

/**
 * Check is there is any of the input parameters that do depend on some
 * other service. 
 */
int dplasma_service_can_be_startup( dplasma_execution_context_t* exec_context )
{
    const dplasma_t* function = exec_context->function;
    param_t* param;
    dep_t* dep;
    int i, j, rc, value;

    for( i = 0; (i < MAX_PARAM_COUNT) && (NULL != function->inout[i]); i++ ) {
        param = function->inout[i];
        if( !(SYM_IN & param->sym_type) ) {
            continue;
        }

        for( j = 0; (j < MAX_DEP_IN_COUNT) && (NULL != param->dep_in[j]); j++ ) {
            dep = param->dep_in[j];

            if( NULL == dep->cond ) {
                if( dep->dplasma->nb_locals != 0 ) {
                    /* Strict dependency on another service. No chance to be a starter */
                    return -1;
                }
                continue;
            }
            /* TODO: Check to see if the condition can be applied in the current context */
            rc = expr_eval( dep->cond, exec_context->locals, MAX_LOCAL_COUNT, &value );
            if( value == 1 ) {
                if( dep->dplasma->nb_locals != 0 ) {
                    return -1;
                }
            }
        }
    }
    return 0;
}

/**
 * Convert the execution context to a string.
 */
char* dplasma_service_to_string( const dplasma_execution_context_t* exec_context,
                                 char* tmp,
                                 size_t length )
{
    const dplasma_t* function = exec_context->function;
    int i, index = 0;

    index += snprintf( tmp + index, length - index, "%s", function->name );
    if( index >= length ) return tmp;
    for( i = 0; i < function->nb_locals; i++ ) {
        index += snprintf( tmp + index, length - index, "_%d",
                           exec_context->locals[i].value );
        if( index >= length ) return tmp;
    }
    /*index += snprintf( tmp + index, length - index, ")" );
      if( index >= length ) return tmp;*/

    return tmp;
}

/**
 * Convert a dependency to a string under the format X(...) -> Y(...).
 */
char* dplasma_dependency_to_string( const dplasma_execution_context_t* from,
                                    const dplasma_execution_context_t* to,
                                    char* tmp,
                                    size_t length )
{
    int index = 0;

    dplasma_service_to_string( from, tmp, length );
    index = strlen(tmp);
    index += snprintf( tmp + index, length - index, " -> " );
    dplasma_service_to_string( to, tmp + index, length - index );
    return tmp;
}

int plasma_show_ranges( const dplasma_t* object )
{
    dplasma_execution_context_t* exec_context = (dplasma_execution_context_t*)malloc(sizeof(dplasma_execution_context_t));
    const expr_t** predicates = (const expr_t**)object->preds;
    int i, nb_locals;

    exec_context->function = (dplasma_t*)object;

    /* Compute the number of local values */
    for( i = nb_locals = 0; (NULL != object->locals[i]) && (i < MAX_LOCAL_COUNT); i++, nb_locals++ );
    if( 0 == nb_locals ) {
        /* special case for the IN/OUT obejcts */
        return 0;
    }
    printf( "Function %s (loops %d)\n", object->name, nb_locals );

    /**
     * This section of code walk through the tree and printout the local and global
     * minimum and maximum values for all local variables.
     */
    for( i = 0; i < nb_locals; i++ ) {
        int abs_min, min, abs_max, max;
        exec_context->locals[i].sym = object->locals[i];
        dplasma_symbol_get_first_value(object->locals[i], predicates,
                                       exec_context->locals, &min);
        exec_context->locals[i].value = min;
        dplasma_symbol_get_last_value(object->locals[i], predicates,
                                      exec_context->locals, &max);

        dplasma_symbol_get_absolute_minimum_value(object->locals[i], &abs_min);
        dplasma_symbol_get_absolute_maximum_value(object->locals[i], &abs_max);

        printf( "Range for local symbol %s is [%d..%d] (global range [%d..%d]) %s\n",
                object->locals[i]->name, min, max, abs_min, abs_max,
                (0 == dplasma_symbol_is_standalone(object->locals[i]) ? "[standalone]" : "[dependent]") );
    }
    return 0;
}

/**
 * This function generate all possible execution context for a given function with
 * respect to the predicates.
 */
int dplasma_compute_nb_tasks( const dplasma_t* object, int use_predicates )
{
    dplasma_execution_context_t* exec_context = (dplasma_execution_context_t*)malloc(sizeof(dplasma_execution_context_t));
    const expr_t** predicates = (const expr_t**)object->preds;
    int rc, actual_loop, nb_tasks = 0;

    exec_context->function = (dplasma_t*)object;

    printf( "Function %s (loops %d)\n", object->name, object->nb_locals );
    if( 0 == object->nb_locals ) {
        /* special case for the IN/OUT obejcts */
        return 0;
    }

    if( 0 != dplasma_set_initial_execution_context(exec_context) ) {
        /* if we can't initialize the execution context then there is no reason to
         * continue.
         */
        return -1;
    }

    /* Clear the predicates if not needed */
    if( !use_predicates ) predicates = NULL;

    actual_loop = object->nb_locals - 1;
    while(1) {
        int value;

        /* Do whatever we have to do for this context */
        {
            char tmp[128];
            printf( "Execute %s\n", dplasma_service_to_string(exec_context, tmp, 128) );
            nb_tasks++;
        }

        /* Go to the next valid value for this loop context */
        rc = dplasma_symbol_get_next_value( object->locals[actual_loop], predicates,
                                            exec_context->locals, &value );

        /* If no more valid values, go to the previous loop,
         * compute the next valid value and redo and reinitialize all other loops.
         */
        if( rc != EXPR_SUCCESS ) {
            int current_loop = actual_loop;
        one_loop_up:
            DEBUG(("Loop index %d based on %s failed to get next value. Going up ...\n",
                   actual_loop, object->locals[actual_loop]->name));
            if( 0 == actual_loop ) {  /* we're done */
                goto end_of_all_loops;
            }
            actual_loop--;  /* one level up */
            rc = dplasma_symbol_get_next_value( object->locals[actual_loop], predicates,
                                                exec_context->locals, &value );
            if( rc != EXPR_SUCCESS ) {
                goto one_loop_up;
            }
            DEBUG(("Keep going on the loop level %d (symbol %s value %d)\n", actual_loop,
                   object->locals[actual_loop]->name, exec_context->locals[actual_loop].value));
            for( actual_loop++; actual_loop <= current_loop; actual_loop++ ) {
                rc = dplasma_symbol_get_first_value(object->locals[actual_loop], predicates,
                                                    exec_context->locals, &value );
                if( rc != EXPR_SUCCESS ) {  /* no values for this symbol in this context */
                    goto one_loop_up;
                }
                DEBUG(("Loop index %d based on %s get first value %d\n", actual_loop,
                       object->locals[actual_loop]->name, exec_context->locals[actual_loop].value));
            }
            actual_loop = current_loop;  /* go back to the original loop */
        } else {
            DEBUG(("Loop index %d based on %s get next value %d\n", actual_loop,
                   object->locals[actual_loop]->name, exec_context->locals[actual_loop].value));
        }
    }
 end_of_all_loops:

    return nb_tasks;
}

/**
 * Resolve all IN() dependencies for this particular instance of execution.
 */
int dplasma_check_IN_dependencies( const dplasma_execution_context_t* exec_context )
{
    const dplasma_t* function = exec_context->function;
    int i, j, rc, value, mask = 0;
    param_t* param;
    dep_t* dep;

    if( !(function->flags & DPLASMA_HAS_IN_IN_DEPENDENCIES) ) {
        return 0;
    }

    for( i = 0; (i < MAX_PARAM_COUNT) && (NULL != function->inout[i]); i++ ) {
        param = function->inout[i];

        if( !(SYM_IN & param->sym_type) ) {
            continue;  /* this is only an OUTPUT dependency */
        }
        for( j = 0; (j < MAX_DEP_IN_COUNT) && (NULL != param->dep_in[j]); j++ ) {
            dep = param->dep_in[j];
            if( NULL != dep->cond ) {
                /* Check if the condition apply on the current setting */
                rc = expr_eval( dep->cond, exec_context->locals, MAX_LOCAL_COUNT, &value );
                if( 0 == value ) {
                    continue;
                }
            }
            if( dep->dplasma->nb_locals == 0 ) {
                mask |= param->param_mask;
            }
        }
    }
    return mask;
}

/**
 * Check if a particular instance of the service can be executed based on the
 * values of the arguments and the ranges specified.
 */
int dplasma_is_valid( dplasma_execution_context_t* exec_context )
{
    dplasma_t* function = exec_context->function;
    int i, rc, min, max;

    for( i = 0; i < function->nb_locals; i++ ) {
        symbol_t* symbol = function->locals[i];

        rc = expr_eval( symbol->min, exec_context->locals, MAX_LOCAL_COUNT, &min );
        if( EXPR_SUCCESS != rc ) {
            fprintf(stderr, " Cannot evaluate the min expression for symbol %s\n", symbol->name);
            return rc;
        }
        rc = expr_eval( symbol->max, exec_context->locals, MAX_LOCAL_COUNT, &max );
        if( EXPR_SUCCESS != rc ) {
            fprintf(stderr, " Cannot evaluate the max expression for symbol %s\n", symbol->name);
            return rc;
        }
        if( (exec_context->locals[i].value < min) ||
            (exec_context->locals[i].value > max) ) {
            char tmp[128];
            fprintf( stderr, "Function %s is not a valid instance.\n",
                     dplasma_service_to_string(exec_context, tmp, 128) );
            return -1;
        }
    }
    return 0;
}

#define CURRENT_DEPS_INDEX(K)  (exec_context->locals[(K)].value - deps->min)

/**
 * Release all OUT dependencies for this particular instance of the service.
 */
int dplasma_release_OUT_dependencies( dplasma_execution_unit_t* eu_context,
                                      const dplasma_execution_context_t* origin,
                                      const param_t* origin_param,
                                      dplasma_execution_context_t* exec_context,
                                      const param_t* dest_param, int forward_remote )
{
    dplasma_t* function = exec_context->function;
    dplasma_dependencies_t *deps, **deps_location, *last_deps;
#ifdef _DEBUG
    char tmp[128];
#endif
    int i, actual_loop, rc;
    static int execution_step = 2;

    if( 0 == function->nb_locals ) {
        /* special case for the IN/OUT objects */
        return 0;
    }

    DEBUG(("Activate dependencies for %s\n", dplasma_service_to_string(exec_context, tmp, 128)));
    deps_location = &(function->deps);
    deps = *deps_location;
    last_deps = NULL;

    for( i = 0; i < function->nb_locals; i++ ) {
    restart_validation:
        rc = dplasma_symbol_validate_value( function->locals[i],
                                            (const expr_t**)function->preds,
                                            exec_context->locals );
        if( 0 != rc ) {
            /* This is a valid value for this parameter, but it is executed 
             * on a remote resource according to the data mapping 
             */
            if(EXPR_FAILURE_CANNOT_EVALUATE_RANGE == rc)
            {
                if(forward_remote)
                {
                   dplasma_remote_dep_activate(eu_context, origin, origin_param, exec_context, dest_param);
                }
            }
            /* This is not a valid value for this parameter on this host. 
             * Try the next one */
        pick_next_value:
            exec_context->locals[i].value++;
            if( exec_context->locals[i].value > exec_context->locals[i].max ) {
                exec_context->locals[i].value = exec_context->locals[i].min;
                if( --i < 0 ) {
                    /* No valid value has been found. Return ! */
                    return -1;
                }
                deps = deps->prev;
                last_deps = deps;
                goto pick_next_value;
            }
            if( 0 == i ) {
                deps_location = &(function->deps);
            } else {
                deps_location = &(deps->u.next[CURRENT_DEPS_INDEX(i)]);
            }
            goto restart_validation;
        }

        if( NULL == (*deps_location) ) {
            int min, max, number;
            /* TODO: optimize this section (and the similar one few tens of lines down
             * the code) to work on local ranges instead of absolute ones.
             */
            dplasma_symbol_get_absolute_minimum_value( function->locals[i], &min );
            dplasma_symbol_get_absolute_maximum_value( function->locals[i], &max );
            /* Make sure we stay in the expected ranges */
            if( exec_context->locals[i].min < min ) {
                DEBUG(("Readjust the minimum range in function %s for argument %s from %d to %d\n",
                       function->name, exec_context->locals[i].sym->name, exec_context->locals[i].min, min));
                exec_context->locals[i].min = min;
                exec_context->locals[i].value = min;
            }
            if( exec_context->locals[i].max > max ) {
                DEBUG(("Readjust the maximum range in function %s for argument %s from %d to %d\n",
                       function->name, exec_context->locals[i].sym->name, exec_context->locals[i].max, max));
                exec_context->locals[i].max = max;
            }
            assert( (min <= exec_context->locals[i].value) && (max >= exec_context->locals[i].value) );
            number = max - min;
            DEBUG(("Allocate %d spaces for loop %s (min %d max %d)\n",
                   number, function->locals[i]->name, min, max));
            deps = (dplasma_dependencies_t*)calloc(1, sizeof(dplasma_dependencies_t) +
                                                   number * sizeof(dplasma_dependencies_union_t));
            if( 0 == dplasma_atomic_cas(deps_location, NULL, deps) ) {
                /* Some other thread manage to set it before us. Not a big deal. */
                free(deps);
                goto deps_created_by_another_thread;
            }
            deps->flags = DPLASMA_DEPENDENCIES_FLAG_ALLOCATED | DPLASMA_DEPENDENCIES_FLAG_FINAL;
            deps->symbol = function->locals[i];
            deps->min = min;
            deps->max = max;
            deps->prev = last_deps; /* chain them backward */
            *deps_location = deps;  /* store the deps in the right location */
            if( NULL != last_deps ) {
                last_deps->flags = DPLASMA_DEPENDENCIES_FLAG_NEXT | DPLASMA_DEPENDENCIES_FLAG_ALLOCATED;
            }
        } else {
        deps_created_by_another_thread:
            deps = *deps_location;
            /* Make sure we stay in bounds */
            if( exec_context->locals[i].min < deps->min ) {
                DEBUG(("Readjust the minimum range in function %s for argument %s from %d to %d\n",
                       function->name, exec_context->locals[i].sym->name, exec_context->locals[i].min, deps->min));
                exec_context->locals[i].min = deps->min;
                exec_context->locals[i].value = deps->min;
            }
            if( exec_context->locals[i].max > deps->max ) {
                DEBUG(("Readjust the maximum range in function %s for argument %s from %d to %d\n",
                       function->name, exec_context->locals[i].sym->name, exec_context->locals[i].max, deps->max));
                exec_context->locals[i].max = deps->max;
            }
        }

        DEBUG(("Prepare storage for next loop variable (value %d) at %d\n",
               exec_context->locals[i].value, CURRENT_DEPS_INDEX(i)));
        deps_location = &(deps->u.next[CURRENT_DEPS_INDEX(i)]);
        last_deps = deps;
    }

    actual_loop = function->nb_locals - 1;
    while(1) {
#ifdef DPLASMA_GENERATE_DOT
        int first_encounter = 0;
#endif  /* DPLASMA_GENERATE_DOT */
        int updated_deps;

        if( 0 != dplasma_is_valid(exec_context) ) {
            char tmp[128], tmp1[128];

            fprintf( stderr, "Output dependencies of %s generate an invalid call to %s for param %s\n",
                     dplasma_service_to_string(origin, tmp, 128),
                     dplasma_service_to_string(exec_context, tmp1, 128), dest_param->name );
            goto next_value;
        }

        /* Mark the dependencies and check if this particular instance can be executed */
        if( !(DPLASMA_DEPENDENCIES_HACK_IN & deps->u.dependencies[CURRENT_DEPS_INDEX(actual_loop)]) ) {
            int mask = dplasma_check_IN_dependencies( exec_context );
            deps->u.dependencies[CURRENT_DEPS_INDEX(actual_loop)] |= mask;
            if( mask > 0 ) {
                DEBUG(("Activate IN dependencies with mask 0x%02x\n", mask));
            }
#ifdef DPLASMA_GENERATE_DOT
            first_encounter = 1;
#endif  /* DPLASMA_GENERATE_DOT */
        }

        updated_deps = dplasma_atomic_bor( &deps->u.dependencies[CURRENT_DEPS_INDEX(actual_loop)],
                                           (DPLASMA_DEPENDENCIES_HACK_IN | dest_param->param_mask));

        if( (updated_deps & (~DPLASMA_DEPENDENCIES_HACK_IN))
            == function->dependencies_mask ) {
#ifdef DPLASMA_GENERATE_DOT
            {
                char tmp[128];
                printf("%s [label=\"%s=>%s\" color=\"%s\" style=\"%s\" headlabel=%d]\n", dplasma_dependency_to_string(origin, exec_context, tmp, 128),
                       origin_param->name, dest_param->name, (first_encounter ? "#00FF00" : "#FF0000"), "solid", execution_step);
            }
#endif  /* DPLASMA_GENERATE_DOT */
            execution_step++;

            /* This service is ready to be executed as all dependencies are solved. Let the
             * scheduler knows about this and keep going.
             */
            __dplasma_schedule(eu_context, exec_context);
        } else {
            DEBUG(("  => Service %s not yet ready (required mask 0x%02x actual 0x%02x: real 0x%02x)\n",
                   dplasma_service_to_string( exec_context, tmp, 128 ), (int)function->dependencies_mask,
                   (int)(updated_deps & (~DPLASMA_DEPENDENCIES_HACK_IN)),
                   (int)(updated_deps)));
#ifdef DPLASMA_GENERATE_DOT
            {
                char tmp[128];
                printf("%s [label=\"%s=>%s\" color=\"%s\" style=\"%s\"]\n", dplasma_dependency_to_string(origin, exec_context, tmp, 128),
                       origin_param->name, dest_param->name, (first_encounter ? "#00FF00" : "#FF0000"), "dashed");
            }
#endif  /* DPLASMA_GENERATE_DOT */
        }

    next_value:
        /* Go to the next valid value for this loop context */
        exec_context->locals[actual_loop].value++;
        if( exec_context->locals[actual_loop].max < exec_context->locals[actual_loop].value ) {
            /* We're out of the range for this variable */
            int current_loop = actual_loop;
        one_loop_up:
            DEBUG(("Loop index %d based on %s failed to get next value. Going up ...\n",
                   actual_loop, function->locals[actual_loop]->name));
            if( 0 == actual_loop ) {  /* we're done */
                goto end_of_all_loops;
            }
            actual_loop--;  /* one level up */
            deps = deps->prev;

            exec_context->locals[actual_loop].value++;
            if( exec_context->locals[actual_loop].max < exec_context->locals[actual_loop].value ) {
                goto one_loop_up;
            }
            DEBUG(("Keep going on the loop level %d (symbol %s value %d)\n", actual_loop,
                   function->locals[actual_loop]->name, exec_context->locals[actual_loop].value));
            deps_location = &(deps->u.next[CURRENT_DEPS_INDEX(actual_loop)]);
            DEBUG(("Prepare storage for next loop variable (value %d) at %d\n",
                   exec_context->locals[actual_loop].value, CURRENT_DEPS_INDEX(actual_loop)));
            for( actual_loop++; actual_loop <= current_loop; actual_loop++ ) {
                exec_context->locals[actual_loop].value = exec_context->locals[actual_loop].min;
                last_deps = deps;  /* save the deps */
                if( NULL == *deps_location ) {
                    int min, max, number;
                    dplasma_symbol_get_absolute_minimum_value( function->locals[actual_loop], &min );
                    dplasma_symbol_get_absolute_maximum_value( function->locals[actual_loop], &max );
                    number = max - min;
                    DEBUG(("Allocate %d spaces for loop %s index %d value %d (min %d max %d)\n",
                           number, function->locals[actual_loop]->name, CURRENT_DEPS_INDEX(actual_loop-1),
                           exec_context->locals[actual_loop].value, min, max));
                    deps = (dplasma_dependencies_t*)calloc(1, sizeof(dplasma_dependencies_t) +
                                                           number * sizeof(dplasma_dependencies_union_t));
                    /**
                     * If we fail then the dependencies array has been allocated by another
                     * thread. Keep going.
                     */
                    if( dplasma_atomic_cas(deps_location, NULL, deps) ) {
                        deps->flags = DPLASMA_DEPENDENCIES_FLAG_ALLOCATED | DPLASMA_DEPENDENCIES_FLAG_FINAL;
                        deps->symbol = function->locals[actual_loop];
                        deps->min = min;
                        deps->max = max;
                        deps->prev = last_deps; /* chain them backward */
                        *deps_location = deps;
                    } else {
                        free(deps);
                    }
                }
                deps = *deps_location;
                deps_location = &(deps->u.next[CURRENT_DEPS_INDEX(actual_loop)]);
                DEBUG(("Prepare storage for next loop variable (value %d) at %d\n",
                       exec_context->locals[actual_loop].value, CURRENT_DEPS_INDEX(actual_loop)));
                last_deps = deps;

                DEBUG(("Loop index %d based on %s get first value %d\n", actual_loop,
                       function->locals[actual_loop]->name, exec_context->locals[actual_loop].value));
            }
            actual_loop = current_loop;  /* go back to the original loop */
        } else {
            DEBUG(("Loop index %d based on %s get next value %d\n", actual_loop,
                   function->locals[actual_loop]->name, exec_context->locals[actual_loop].value));
        }
    }
 end_of_all_loops:

    return 0;
}
