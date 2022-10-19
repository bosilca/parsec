/*
 * Copyright (c) 2012-2019 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 */

#include "parsec/task_class.h"


// Reminder: definitions:

// /**
//  * @brief Opaque structure representing a Task Class
//  */
// typedef struct parsec_task_class_s      parsec_task_class_t;

// typedef struct parsec_flow_s parsec_flow_t;
// typedef struct parsec_dep_s parsec_dep_t;

// struct parsec_task_class_s {
//     const char                  *name;

//     uint16_t                     flags;
//     uint8_t                      task_class_id;  /**< index in the dependency and in the function array */

//     uint8_t                      nb_flows;
//     uint8_t                      nb_parameters;
//     uint8_t                      nb_locals;

//     uint8_t                      task_class_type;

//     parsec_dependency_t          dependencies_goal;
//     const parsec_symbol_t       *params[MAX_LOCAL_COUNT];
//     const parsec_symbol_t       *locals[MAX_LOCAL_COUNT];
//     const parsec_flow_t         *in[MAX_PARAM_COUNT];
//     const parsec_flow_t         *out[MAX_PARAM_COUNT];
//     const parsec_expr_t         *priority;
//     const parsec_property_t     *properties;     /**< {NULL, NULL} terminated array of properties holding all function-specific properties expressions */

//     parsec_data_ref_fn_t        *initial_data;   /**< Populates an array of data references, of maximal size MAX_PARAM_COUNT */
//     parsec_data_ref_fn_t        *final_data;     /**< Populates an array of data references, of maximal size MAX_PARAM_COUNT */
//     parsec_data_ref_fn_t        *data_affinity;  /**< Populates an array of data references, of size 1 */
//     parsec_key_fn_t             *key_functions;
//     parsec_functionkey_fn_t     *make_key;
//     parsec_printtask_fn_t       *task_snprintf;
// #if defined(PARSEC_SIM)
//     parsec_sim_cost_fct_t       *sim_cost_fct;
// #endif
//     parsec_datatype_lookup_t    *get_datatype;
//     parsec_hook_t               *prepare_input;
//     const __parsec_chore_t      *incarnations;
//     parsec_hook_t               *prepare_output;

//     parsec_find_dependency_fn_t   *find_deps;
//     parsec_update_dependency_fn_t *update_deps;

//     parsec_traverse_function_t  *iterate_successors;
//     parsec_traverse_function_t  *iterate_predecessors;
//     parsec_release_deps_t       *release_deps;
//     parsec_hook_t               *complete_execution;
//     parsec_new_task_function_t  *new_task;
//     parsec_hook_t               *release_task;
//     parsec_hook_t               *fini;
// };


// struct parsec_flow_s {
//     char               *name;
//     uint8_t             sym_type;
//     uint8_t             flow_flags;
//     uint8_t             flow_index; /**< The input index of the flow. This index is used
//                                      *   while computing the mask. */
//     parsec_dependency_t flow_datatype_mask;  /**< The bitmask of dep_datatype_index of all deps */

// // if parametrized flows, we need to be able to modify dep indices
// #if defined(PARSEC_ALLOW_PARAMETRIZED_FLOWS)
//     parsec_dep_t *dep_in[MAX_DEP_IN_COUNT];
//     parsec_dep_t *dep_out[MAX_DEP_OUT_COUNT];
// #else
//     parsec_dep_t const *dep_in[MAX_DEP_IN_COUNT];
//     parsec_dep_t const *dep_out[MAX_DEP_OUT_COUNT];
// #endif
// };

// struct parsec_dep_s {
//     parsec_expr_t const        *cond;           /**< The runtime-evaluable condition on this dependency */
//     parsec_expr_t const        *ctl_gather_nb;  /**< In case of control gather, the runtime-evaluable number of controls to expect */
//     uint8_t                    task_class_id;   /**< Index of the target parsec function in the object function array */
//     uint8_t                    dep_index;      /**< Output index of the dependency. This is used to store the flow
//                                                 *   before transfering it to the successors. */
//     uint8_t                    dep_datatype_index;  /**< Index of the output datatype. */
//     parsec_flow_t const        *flow;           /**< Pointer to the flow pointed to/from this dependency */
//     parsec_flow_t const        *belongs_to;     /**< The flow this dependency belongs tp */
//     parsec_data_lookup_func_t  direct_data;    /**< Lookup the data associated with this dep, if (and only if)
//                                                 *   this dep is a direct memory access */
// };






/* Prints the task class information (for debugging purposes)
 * It will print the basic task information, its flows and their deps
 */
void parsec_debug_dump_task_class_at_exec(parsec_task_class_t *tc)
{
    int i, j, k;
    parsec_flow_t *flow;
    parsec_dep_t *dep;

    parsec_debug_verbose(1, parsec_debug_output, "###### PRINTING TASK CLASS %s ######", tc->name);

    parsec_debug_verbose(1, parsec_debug_output, "Task Class %s (%p) has %d flows, %d parameters, %d locals",
                         tc->name, (void*)tc, tc->nb_flows, tc->nb_parameters, tc->nb_locals);

    for(i = 0; i < tc->nb_flows; i++) {
        flow = (parsec_flow_t*)tc->in[i];
        if(flow)
        {
            parsec_debug_verbose(1, parsec_debug_output, "  Input of flow %s (%p)",
                                flow->name, (void*)flow);
            for(j = 0; j < MAX_DEP_IN_COUNT && flow->dep_in[j]; j++) {
                dep = (parsec_dep_t*)flow->dep_in[j];
                if(!dep)
                {
                    continue;
                }
                if(dep->flow)
                {
                    parsec_debug_verbose(1, parsec_debug_output, "    Input dep %d of flow %s is an input dep that receives data from dep %d of flow %s (id=%d) of task class %d",
                                        j, flow->name, dep->dep_index, dep->flow->name, dep->flow->flow_index,
                                        dep->task_class_id);
                }
                else
                {
                    parsec_debug_verbose(1, parsec_debug_output, "    Input dep %d of flow %s is an input dep that receives data from no one",
                                        j, flow->name);
                }
                parsec_debug_verbose(1, parsec_debug_output, "      datatype=%d, direct_data=%p",
                                    dep->dep_datatype_index, (void*)dep->direct_data);
            }
        }
    }
    for(i = 0; i < tc->nb_flows; i++) {
        flow = (parsec_flow_t*)tc->out[i];
        if(flow)
        {
            parsec_debug_verbose(1, parsec_debug_output, "  Output of flow %s (%p)",
                                flow->name, (void*)flow);
            for(j = 0; j < MAX_DEP_OUT_COUNT && flow->dep_out[j]; j++) {
                dep = (parsec_dep_t*)flow->dep_out[j];
                if(!dep)
                {
                    continue;
                }
                if(dep->flow)
                {
                    parsec_debug_verbose(1, parsec_debug_output, "    Output dep %d of flow %s is an output dep that sends data to dep %d of flow %s (id=%d) of task class %d",
                                        j, flow->name, dep->dep_index, dep->flow->name, dep->flow->flow_index,
                                        dep->task_class_id);
                }
                else
                {
                    parsec_debug_verbose(1, parsec_debug_output, "    Output dep %d of flow %s is an output dep that sends data to no one",
                                        j, flow->name);
                }
                parsec_debug_verbose(1, parsec_debug_output, "      datatype=%d, direct_data=%p",
                                    dep->dep_datatype_index, (void*)dep->direct_data);
            }
        }
    }
}
