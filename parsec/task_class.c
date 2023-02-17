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
//     const parsec_flow_t         *in[MAX_DATAFLOWS_PER_TASK];
//     const parsec_flow_t         *out[MAX_DATAFLOWS_PER_TASK];
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

bool parsec_helper_flow_is_in_flow_array(const parsec_flow_t *flow, parsec_flow_t *flow_array[], int flow_array_size)
{
    for (int i = 0; i < flow_array_size; i++)
    {
        if (flow == flow_array[i])
        {
            return true;
        }
    }
    return false;
}

bool parsec_helper_dep_is_in_flow_array(const parsec_dep_t *dep, parsec_dep_t *dep_array[], int dep_array_size)
{
    for (int i = 0; i < dep_array_size; i++)
    {
        if (dep == dep_array[i])
        {
            return true;
        }
    }
    return false;
}

/** __parsec_LBM_shift_all_flow_reference_after
 *
 * Shift all the flows after "pivot_flow" by "shift"
 * If in_out==0, shift the input flows, otherwise shift the output flows
 *
 */
void parsec_shift_all_flows_after(parsec_task_class_t *tc, const parsec_flow_t *pivot_flow, int shift)
{
    int i, j;
    int flow_in_out;
    int pivot_index;
    int last_flow_index;
    parsec_flow_t *flow;
    parsec_dep_t *dep;

    // use an array to keep track of the flows we already treated
    parsec_flow_t *shifted_flows[MAX_DATAFLOWS_PER_TASK];
    int shifted_flows_size = 0;

    // Determine which array should be used (in or out) depending on in_out
    //parsec_flow_t **flow_array = (parsec_flow_t **) ((in_out) ? (tc->out) : (tc->in));
    parsec_dep_t **dep_array;

    // The flows will be shifted both in th "in" and "out" arrays
    for (int in_out = 0; in_out < 2; ++in_out) {
        const int max_dep_count = (in_out) ? MAX_DEP_OUT_COUNT : MAX_DEP_IN_COUNT;

        // find the pivot flow
        for (i = 0; i < MAX_DATAFLOWS_PER_TASK; ++i)
        {
            if(pivot_flow == ((in_out) ? (tc->out) : (tc->in))[i])
                break; // pivot flow found
            if(!((in_out) ? (tc->out) : (tc->in))[i])
                break; // pivot not found
        }
        if(pivot_flow == ((in_out) ? (tc->out) : (tc->in))[i])
        {
            pivot_index = i;

            // - Update all the dep_index > pivot_index (MODIFICATION: DONT TO THIS)
            // - find last_flow_index (needed for performing the actual shift at the end)
            for (i = pivot_index + 1; i < MAX_DATAFLOWS_PER_TASK; i++)
            {
                flow = ((in_out) ? (tc->out) : (tc->in))[i];
                if(!flow)
                    break; // end of the array

                dep_array = (in_out) ? flow->dep_out : flow->dep_in;

                for(j=0; j<max_dep_count; j++)
                {
                    dep = dep_array[j];

                    if(!dep)
                        break; // no more dep
                    // if(dep->dep_index > pivot_index) // MODIFICATION: DONT TO THIS
                    //     dep->dep_index += shift*(i-pivot_index);
                }
                if(!flow)
                    break; // end of the array
            }
            last_flow_index = i - 1;

            // - Shift the flows
            // - Update the flow_index of the flows
            for (i = last_flow_index; i > pivot_index; i--)
            {
                flow = ((in_out) ? (tc->out) : (tc->in))[i];
                ((in_out) ? (tc->out) : (tc->in))[i+shift] = flow;
                if(!parsec_helper_flow_is_in_flow_array(flow, shifted_flows, shifted_flows_size))
                {
                    flow->flow_index += shift;
                    shifted_flows[shifted_flows_size] = flow;
                    shifted_flows_size++;
                }
            }
        }
    }

/*
    for (i = 0; i < MAX_DATAFLOWS_PER_TASK; i++)
    {
        if(pivot_flow == tc->in[i])
            break;
    }
    //if(i < MAX_DATAFLOWS_PER_TASK) {
    for(;i<MAX_DATAFLOWS_PER_TASK;++i)
    {
        for(j=0;j<MAX_DEP_IN_COUNT;++j)
        {
            tc->in[i]->dep[j]->dep_index += shift;
        }
    }
    //}
*/







/*
    // Increase the IDs of every flow that is greater than the ID of pivot_flow
    for (flow_in_out = 0; flow_in_out < 2; ++flow_in_out)
    {
        for (i = 0; i < MAX_DATAFLOWS_PER_TASK; i++)
        {
            flow = (parsec_flow_t *)(flow_in_out ? tc->out[i] : tc->in[i]);
            if (parsec_helper_flow_is_in_flow_array(flow, treated_flows, treated_flows_size))
            {
                continue;
            }

            if (flow)
            {
                if (flow->flow_index > pivot_flow->flow_index)
                {
                    flow->flow_index += shift;
                }

                treated_flows[treated_flows_size] = flow;
                ++treated_flows_size;
            }
            else
            {
                break;
            }
        }
    }

    /* We perform the shift on both the in and out flows of the task class
     * The pivot flow can have different indices in the in and out arrays
     *
     * Note: We displace all the flows following the pivot flow in the in and out array
     * But this is just for convience, to get the parametrized subflows next to each other
     *//*
    for (flow_in_out = 0; flow_in_out < 2; ++flow_in_out)
    {

        // Look for the pivot flow
        pivot_index = -1;
        for (i = 0; i < MAX_DATAFLOWS_PER_TASK; i++)
        {
            flow = (parsec_flow_t *)(flow_in_out ? tc->out[i] : tc->in[i]);

            if (!flow)
            {
                break;
            }

            if (flow == pivot_flow)
            {
                pivot_index = i;
                break;
            }
        }

        // If the pivot was not found in this flow_in_out, skip the shift
        if (pivot_index == -1)
        {
            continue;
        }

        // Find the last non-null flow
        for (i = pivot_index; i < MAX_DATAFLOWS_PER_TASK; i++)
        {
            flow = (parsec_flow_t *)(flow_in_out ? tc->out[i] : tc->in[i]);

            if (!flow)
            {
                break;
            }
        }
        int last_flow_index = i - 1;

        // Shift all the flows after the pivot
        for (i = last_flow_index; i > pivot_index; i--)
        {
            flow = (parsec_flow_t *)(flow_in_out ? tc->out[i] : tc->in[i]);

            assert(flow); // We should not have a NULL flow here
            // assert(flow->flow_index == i); // The flow index can acutally be anything
            assert(i + shift < MAX_DATAFLOWS_PER_TASK); // We should not overflow the array

            // Shift the flow
            (flow_in_out ? tc->out : tc->in)[i + shift] = flow;
        }
    }*/
}

void parsec_shift_all_deps_after_and_update_tc(parsec_task_class_t *tc, parsec_flow_t *flow, parsec_dep_t *pivot_dep, int dep_in_out, int shift)
{
    int pivot_dep_index;

    // Look for the pivot dep
    pivot_dep_index = -1;
    for (int i = 0; i < (dep_in_out?MAX_DEP_OUT_COUNT:MAX_DEP_IN_COUNT); i++)
    {
        parsec_dep_t *dep = (parsec_dep_t *)(dep_in_out ? flow->dep_out[i] : flow->dep_in[i]);

        if (!dep)
        {
            break;
        }

        if (dep == pivot_dep)
        {
            pivot_dep_index = i;
            break;
        }
    }

    // If the pivot was not found in this flow_in_out, skip the shift
    if (pivot_dep_index == -1)
    {
        assert(0);
        return;
    }

    assert((dep_in_out ? flow->dep_out[pivot_dep_index] : flow->dep_in[pivot_dep_index]) == pivot_dep); // From there, the pivot dep should be in the array

    // Find the last non-null dep
    int last_dep_index;
    for (last_dep_index = pivot_dep_index + 1; last_dep_index < (dep_in_out?MAX_DEP_OUT_COUNT:MAX_DEP_IN_COUNT); ++last_dep_index)
    {
        parsec_dep_t *dep = (parsec_dep_t *)(dep_in_out ? flow->dep_out[last_dep_index] : flow->dep_in[last_dep_index]);

        if (!dep)
        {
            break;
        }
    }
    last_dep_index--;

    // Shift all the deps after the pivot
    for (int i = last_dep_index; i > pivot_dep_index; i--)
    {
        parsec_dep_t *dep = (parsec_dep_t *)(dep_in_out ? flow->dep_out[i] : flow->dep_in[i]);

        assert(dep); // We should not have a NULL dep here
        // assert(dep->dep_index == i); // The dep index can acutally be anything
        assert(i + shift < (dep_in_out?MAX_DEP_OUT_COUNT:MAX_DEP_IN_COUNT)); // We should not overflow the array

        dep->dep_index += shift;

        // Shift the dep
        (dep_in_out ? flow->dep_out : flow->dep_in)[i + shift] = dep;
    }


/*
    // keep track of the deps that have been treated
    parsec_dep_t *treated_deps[MAX_DEP_IN_COUNT + MAX_DEP_OUT_COUNT];
    int treated_deps_size = 0;

    for(int in_out=0;in_out<2;++in_out)
    {
        for(int flid=0; flid<MAX_DATAFLOWS_PER_TASK; ++flid)
        {
            parsec_flow_t *fl = (parsec_flow_t *)(in_out ? tc->out[flid] : tc->in[flid]);
            if(!fl)
                break;

            int pivot_dep_dep_index = pivot_dep->dep_index;
            for (int depid = 0; depid < (in_out?MAX_DEP_OUT_COUNT:MAX_DEP_IN_COUNT); depid++)
            {
                parsec_dep_t *dep = (parsec_dep_t *)(in_out ? fl->dep_out[depid] : fl->dep_in[depid]);

                if (!dep)
                    break;

                if (dep->dep_index > pivot_dep_dep_index && !parsec_helper_dep_is_in_flow_array(dep, treated_deps, treated_deps_size))
                {
                    dep->dep_index += shift;
                    treated_deps[treated_deps_size++] = dep;
                }
            }
        }
    }
*/

    /* // The datatype_mask should stay the same, as we do not add any datatype
    // Also shift the flow's flow_datatype_mask
    if(dep_in_out)
    {
        // If output dep
        // Goal: 000xxxxx -> 0xxxx11x (if shift=1 and pivot_dep_index=1)
        // (add 1's in between)
        parsec_dependency_t unshifted_values = flow->flow_datatype_mask & ((1<<pivot_dep_index)-1);
        parsec_dependency_t shifted_values = (flow->flow_datatype_mask >> (pivot_dep_index + 1)) << (pivot_dep_index + 1 + shift);
        parsec_dependency_t in_between = ((1<<(shift+1))-1) << pivot_dep_index;
        flow->flow_datatype_mask = unshifted_values | shifted_values | in_between;
    }
    */
}

/* Prints the task class information (for debugging purposes)
 * It will print the basic task information, its flows and their deps
 */
void parsec_debug_dump_task_class_at_exec(parsec_task_class_t *tc)
{
    int i, j;
    int flow_in_out, dep_in_out;
    parsec_flow_t *flow;
    parsec_dep_t *dep;

    // flows can appear twice in a task class (if both in and out)
    parsec_flow_t *treated_flows[MAX_DATAFLOWS_PER_TASK];
    int treated_flows_size = 0;

    parsec_debug_verbose(1, parsec_debug_output, "###### PRINTING TASK CLASS %s ######", tc->name);

    parsec_debug_verbose(1, parsec_debug_output, "## Task Class %s (%p) has %d flows, %d parameters, %d locals",
                         tc->name, (void *)tc, tc->nb_flows, tc->nb_parameters, tc->nb_locals);

    parsec_debug_verbose(1, parsec_debug_output, "## dependencies_goal = %x", tc->dependencies_goal);

    for (i = 0; i < MAX_DATAFLOWS_PER_TASK; i++)
    {
        for (flow_in_out = 0; flow_in_out < 2; ++flow_in_out)
        {
            flow = (parsec_flow_t *)(flow_in_out ? tc->out[i] : tc->in[i]);

            if (flow && !parsec_helper_flow_is_in_flow_array(flow, treated_flows, treated_flows_size))
            {
                parsec_debug_verbose(1, parsec_debug_output, "  RW flow %s (addr=%p, id=%d, flow_datatype_mask=%p, flow_flags=%p)",
                                     flow->name, (void *)flow, flow->flow_index, flow->flow_datatype_mask, flow->flow_flags);
                for (dep_in_out = 0; dep_in_out < 2; ++dep_in_out)
                {
                    for (j = 0; j < (dep_in_out ? MAX_DEP_OUT_COUNT : MAX_DEP_IN_COUNT); j++)
                    {
                        dep = (parsec_dep_t *)(dep_in_out ? flow->dep_out[j] : flow->dep_in[j]);
                        if (!dep)
                        {
                            continue;
                        }

                        if (PARSEC_LOCAL_DATA_TASK_CLASS_ID == dep->task_class_id)
                        {
                            parsec_debug_verbose(1, parsec_debug_output, "    %s dep [%d] (addr=%p) of flow %s linked with data collection",
                                                 dep_in_out ? "->" : "<-", j, dep, flow->name);
                        }
                        else if (dep->flow)
                        {
                            parsec_debug_verbose(1, parsec_debug_output, "    %s dep [%d] (addr=%p) of flow %s has dep_id=%d and goes to flow %s (id=%d) of task class %d",
                                                 dep_in_out ? "->" : "<-", j, dep, flow->name, dep->dep_index, dep->flow->name,
                                                 dep->flow->flow_index, dep->task_class_id);
                        }
                        else
                        {
                            parsec_debug_verbose(1, parsec_debug_output, "## WARNING ## , parsec_debug_dump_task_class_at_exec does not know this type of dependency");
                            continue;
                        }
                        parsec_debug_verbose(1, parsec_debug_output, "\t\tdatatype=%d\tdirect_data=%p\tdep_datatype_index=%d\tdep_index=%d\ttask_class_id=%d",
                                             dep->dep_datatype_index, (void *)dep->direct_data, dep->dep_datatype_index, dep->dep_index, dep->task_class_id);
                    }
                }

                treated_flows[treated_flows_size] = flow;
                ++treated_flows_size;
            }
        }
    }
}

/* Checks if the task class is valid (for debugging purposes)
 */
void parsec_check_sanity_of_task_class(parsec_task_class_t *tc, bool check_dep_index)
{
    int i, j;
    int flow_in_out, dep_in_out;
    parsec_dep_t *dep;

    // flows can appear twice in a task class (if both in and out)
    parsec_flow_t *treated_flows[MAX_DATAFLOWS_PER_TASK];
    int treated_flows_size = 0;

    for (i = 0; i < MAX_DATAFLOWS_PER_TASK; i++)
    {
        for (flow_in_out = 0; flow_in_out < 2; ++flow_in_out)
        {
            const parsec_flow_t *flow = (parsec_flow_t *)(flow_in_out ? tc->out[i] : tc->in[i]);

            if (flow && !parsec_helper_flow_is_in_flow_array(flow, treated_flows, treated_flows_size))
            {
                // TODO ? Is there anything to assert?

                treated_flows[treated_flows_size] = flow;
                ++treated_flows_size;
            }
        }
    }

    // for each flow and each dep, check that:
    // - belongs_to and flow : != 0xdeadbeef and != NULL
    for(int in_out=0; in_out<2; ++in_out) {
        for (i = 0; i < MAX_DATAFLOWS_PER_TASK; i++)
        {
            parsec_flow_t *flow = (parsec_flow_t *)(in_out ? tc->out[i] : tc->in[i]);
            if(!flow) continue;
            for(int j=0;j<(in_out?MAX_DEP_OUT_COUNT:MAX_DEP_IN_COUNT);++j) {
                dep = (parsec_dep_t *)(in_out ? flow->dep_out[j] : flow->dep_in[j]);
                if(!dep) continue;
                assert(dep->belongs_to != (void*)0xdeadbeef);
                assert(dep->belongs_to != NULL);
                assert(dep->flow != (void*)0xdeadbeef);
                assert(dep->flow != NULL || dep->task_class_id == PARSEC_LOCAL_DATA_TASK_CLASS_ID);
            }
        }
    }

    // Check the coherency of the flow flags
    if(check_dep_index)
    {
        for (i = 0; i < MAX_DATAFLOWS_PER_TASK; i++)
        {
            const parsec_flow_t *flow = tc->out[i];

            if (!flow)
            {
                break;
            }

            // For each output dep of the flow ...
            for (j = 0; j < (dep_in_out ? MAX_DEP_OUT_COUNT : MAX_DEP_IN_COUNT); j++)
            {
                dep = (parsec_dep_t *)(dep_in_out ? flow->dep_out[j] : flow->dep_in[j]);
                if (!dep)
                {
                    break;
                }

                if(dep_in_out == 1)
                {
                    // All out dependencies should be in the flow datatype mask
                    assert((1 << dep->dep_datatype_index) & flow->flow_datatype_mask);
                    //assert((1 << dep->dep_index) & flow->flow_datatype_mask);

                    // All out dependencies should be mappable in the flow datatype mask
                    assert(dep->dep_datatype_index < sizeof(flow->flow_datatype_mask) * 8);
                }

                assert(dep->dep_index < (dep_in_out ? MAX_DEP_OUT_COUNT : MAX_DEP_IN_COUNT));
            }
        }
    }

    assert(tc->nb_flows == treated_flows_size);
}

parsec_flow_t *parsec_helper_copy_flow(parsec_flow_t *flow_to, const parsec_flow_t *flow_from)
{
    int flow_in_out;

    assert(flow_to);
    memcpy(flow_to, flow_from, sizeof(parsec_flow_t));

    // Copy the dependencies
    int i;
    for (flow_in_out = 0; flow_in_out < 2; ++flow_in_out)
    {
        for (i = 0; i < (flow_in_out ? MAX_DEP_OUT_COUNT : MAX_DEP_IN_COUNT); i++)
        {
            parsec_dep_t *dep = (parsec_dep_t *)(flow_in_out ? flow_to->dep_out[i] : flow_to->dep_in[i]);

            if (!dep)
            {
                break;
            }

            // Not copying the dep should be fine:
            /*parsec_dep_t *new_dep = (parsec_dep_t *)malloc(sizeof(parsec_dep_t));
            assert(new_dep);
            memcpy(new_dep, dep, sizeof(parsec_dep_t));
            (flow_in_out ? flow_to->dep_out : flow_to->dep_in)[i] = new_dep;*/
        }
    }

    return flow_to;
}

parsec_dep_t *parsec_helper_copy_dep(parsec_dep_t * dep_to, const parsec_dep_t * dep_from)
{
    //parsec_dep_t *new_dep = (parsec_dep_t *)malloc(sizeof(parsec_dep_t));
    assert(dep_to);
    memcpy(dep_to, dep_from, sizeof(parsec_dep_t));
    return dep_to;
}

int parsec_helper_dep_is_in_flow(const parsec_flow_t *flow, const parsec_dep_t *dep, int in_out)
{
    int i;
    for (i = 0; i < (in_out ? MAX_DEP_OUT_COUNT : MAX_DEP_IN_COUNT); i++)
    {
        if (dep == (in_out ? flow->dep_out[i] : flow->dep_in[i]))
        {
            return 1;
        }
    }
    return 0;
}

int parsec_helper_get_dep_index_in_flow(const parsec_flow_t *flow, const parsec_dep_t *dep, int in_out)
{
    int i;
    for (i = 0; i < (in_out ? MAX_DEP_OUT_COUNT : MAX_DEP_IN_COUNT); i++)
    {
        if (dep == (in_out ? flow->dep_out[i] : flow->dep_in[i]))
        {
            return i;
        }
    }
    assert(0);
    return -1;
}

int parsec_helper_get_dep_index(const parsec_task_class_t *tc, const parsec_dep_t *dep, int in_out)
{
    int i;
    for (i = 0; i < MAX_DATAFLOWS_PER_TASK; i++)
    {
        parsec_flow_t *flow = (parsec_flow_t *)(in_out ? tc->out[i] : tc->in[i]);
        if (!flow)
        {
            break;
        }

        if (parsec_helper_dep_is_in_flow(flow, dep, in_out))
        {
            return parsec_helper_get_dep_index_in_flow(flow, dep, in_out);
        }
    }

    assert(0);
    return -1;
}

int parsec_helper_get_flow_index_that_contains_dep(const parsec_task_class_t *tc, const parsec_dep_t *dep, int in_out)
{
    int i;
    for (i = 0; i < MAX_DATAFLOWS_PER_TASK; i++)
    {
        const parsec_flow_t *flow = (in_out ? tc->out[i] : tc->in[i]);

        if (!flow)
        {
            break;
        }

        if (parsec_helper_dep_is_in_flow(flow, dep, in_out))
        {
            return i;
        }
    }

    assert(0);
    return -1;
}

int parsec_helper_get_flow_index_in_or_out(const parsec_task_class_t *tc, const parsec_flow_t *flow)
{
    int i = parsec_helper_get_flow_index(tc, flow, 1);
    i =  ((i == -1) ? parsec_helper_get_flow_index(tc, flow, 0) : i);
    assert(i != -1);
    return i;
}

int parsec_helper_get_flow_index(const parsec_task_class_t *tc, const parsec_flow_t *flow, int in_out)
{
    int i;
    for (i = 0; i < MAX_DATAFLOWS_PER_TASK; i++)
    {
        const parsec_flow_t *_flow = (in_out ? tc->out[i] : tc->in[i]);

        if (!flow)
        {
            break;
        }

        if (flow == _flow)
        {
            return i;
        }
    }

    //assert(0);
    return -1;
}
