#ifndef _jdf2c_utils_h
#define _jdf2c_utils_h

#include "string_arena.h"

typedef char *(*dumper_function_t)(void **elt, void *arg);


/**
 * FLOW_IS_PARAMETRIZED:
 *    Tells whether a flow is parametrized or not.
 *  @param [IN] flow:         the flow to test.
 *
 *  @return a boolean value.
 */
#define FLOW_IS_PARAMETRIZED(flow) \
    ((flow)->local_variables != NULL)

/**
 * GET_PARAMETRIZED_FLOW_ITERATOR_NAME
 * @param flow: the flow from which to get the iterator
 * 
 * Returns a string, the name of the iterator
 */
#define GET_PARAMETRIZED_FLOW_ITERATOR_NAME(flow) \
    get_parametrized_flow_iterator_name(flow)

static inline char *get_parametrized_flow_iterator_name(jdf_dataflow_t const *flow)
{
    assert(FLOW_IS_PARAMETRIZED(flow));
    assert(flow->local_variables->next == NULL); // only one iterator for parametrized flows

    return flow->local_variables->alias;
}

/**
 * INDENTATION_IF_PARAMETRIZED:
 *    Returns an indentation string if the flow is parametrized.
 *  @param [IN] flow:         said flow.
 *
 *  @return a string containing an indentation if the flow is parametrized, an empty string otherwise.
 */
#define INDENTATION_IF_PARAMETRIZED(flow) \
    ((FLOW_IS_PARAMETRIZED(flow)) ? "  " : "")

/**
 * DUMP_ARRAY_OFFSET_IF_PARAMETRIZED:
 *    Tells whether a flow is parametrized or not.
 *  @param [IN] sa:           the string arena to use.
 *  @param [IN] flow:         the flow to test.
 *
 *  @return an empty string if not parametrized, [var] else (var being the name of the iterator variable).
 */
#define DUMP_ARRAY_OFFSET_IF_PARAMETRIZED(sa, flow) \
    util_dump_array_offset_if_parametrized(sa, flow)

/**
 * util_dump_array_offset_if_parametrized:
 *   function used by the DUMP_ARRAY_OFFSET_IF_PARAMETRIZED* macros. Do not use directly.
 */
static inline char*
util_dump_array_offset_if_parametrized(string_arena_t *sa, const jdf_dataflow_t *flow)
{
    // reinit sa
    string_arena_init(sa);

    if (FLOW_IS_PARAMETRIZED(flow)) {
        string_arena_add_string(sa, "[%s]", get_parametrized_flow_iterator_name(flow));
    }

    return string_arena_get_string(sa);
}

/** 
 * VARIABLE_IS_FLOW_LEVEL
 *   Tells whether a variable is a flow level variable or not.
 * @param [IN] var:           the variable to test.
 * @param [IN] flow:          the flow to test.
 * 
 * @return a boolean value.
 */
#define VARIABLE_IS_FLOW_LEVEL(flow, var) \
    variable_is_flow_level_util(flow, var)

static inline int variable_is_flow_level_util(const jdf_dataflow_t *flow, const jdf_expr_t *var)
{
    for(jdf_expr_t *flow_variable=flow->local_variables; flow_variable!=NULL; flow_variable=flow_variable->next) {
        if (strcmp(flow_variable->alias, var->alias) == 0) {
            return 1;
        }
    }

    return 0;
}

/**
 * JDF_ANY_FLOW_IS_PARAMETRIZED:
 *   Tells whether any flow is parametrized or not.
 *   Used to avoid code overloading if no paramtrized flow is present.
 * @param [IN] jdf:           the jdf to test.
 * 
 * @return a boolean value.
 */
#define JDF_ANY_FLOW_IS_PARAMETRIZED(jdf) \
    jdf_any_flow_is_parametrized_util(jdf)

static inline int jdf_any_flow_is_parametrized_util(const jdf_t *jdf)
{
    for( jdf_function_entry_t* f = jdf->functions; NULL != f; f = f->next ) {
        for( jdf_dataflow_t* df = f->dataflow; NULL != df; df = df->next ) {
            if( FLOW_IS_PARAMETRIZED(df) ) {
                return 1;
            }
        }
    }

    return 0;
}

/**
 * TASK_CLASS_ANY_FLOW_IS_PARAMETRIZED:
 *  Tells whether any flow is parametrized or not.
 * 
 * @param [IN] tc:            the task class to test.
 * 
 * @return a boolean value.
 */
#define TASK_CLASS_ANY_FLOW_IS_PARAMETRIZED(tc) \
    task_class_any_flow_is_parametrized_util(tc)

static inline int task_class_any_flow_is_parametrized_util(const jdf_function_entry_t *tc)
{
    for( jdf_dataflow_t* df = tc->dataflow; NULL != df; df = df->next ) {
        if( FLOW_IS_PARAMETRIZED(df) ) {
            return 1;
        }
    }

    return 0;
}



/**
 * UTIL_DUMP_LIST_FIELD:
 *    Iterate over the elements of a list, transforming each field element in a string using a parameter function,
 *    and concatenate all strings.
 *    The function has the prototype  field_t **e, void *a -> char *strelt
 *    The final string has the format
 *       before (prefix strelt separator)* (prefix strelt) after
 *  @param [IN] arena:         string arena to use to add strings elements to the final string
 *  @param [IN] structure_ptr: pointer to a structure that implement any list
 *  @param [IN] nextfield:     the name of a field pointing to the next structure pointer
 *  @param [IN] eltfield:      the name of a field pointing to an element to print
 *  @param [IN] fct:           a function that transforms a pointer to an element to a string of characters
 *  @param [IN] fctarg:        fixed argument of the function
 *  @param [IN] before:        string (of characters) representing what must appear before the list
 *  @param [IN] prefix:        string (of characters) representing what must appear before each element
 *  @param [IN] separator:     string (of characters) that will be put between each element, but not at the end
 *                             or before the first
 *  @param [IN] after:         string (of characters) that will be put at the end of the list, after the last
 *                             element
 *
 *  @return a string (of characters) written in arena with the list formed so.
 *
 *  If the function fct return NULL, the element is ignored
 *
 *  Example: to create the list of expressions that is a parameter call, use
 *    UTIL_DUMP_LIST_FIELD(sa, jdf->functions->predicates, next, expr, dump_expr, NULL, "(", "", ", ", ")")
 *  Example: to create the list of declarations of globals, use
 *    UTIL_DUMP_LIST_FIELD(sa, jdf->globals, next, name, dumpstring, NULL, "", "  int ", ";\n", ";\n");
 */
#define UTIL_DUMP_LIST_FIELD(arena, structure_ptr, nextfield, eltfield, fct, fctarg, before, prefix, separator, after) \
    util_dump_list_fct( arena, structure_ptr,                           \
                        (char *)&(structure_ptr->nextfield)-(char *)structure_ptr, \
                        (char *)&(structure_ptr->eltfield)-(char *)structure_ptr, \
                            fct, fctarg, before, prefix, separator, after)

/**
 * UTIL_DUMP_LIST:
 *    Iterate over the elements of a list, transforming each element in a string using a parameter function,
 *    and concatenate all strings.
 *    The function has the prototype  list_elt_t **e, void *a -> char *strelt
 *    The final string has the format
 *       before (prefix strelt separator)* (prefix strelt) after
 *  @param [IN] arena:         string arena to use to add strings elements to the final string
 *  @param [IN] structure_ptr: pointer to a structure that implement any list
 *  @param [IN] nextfield:     the name of a field pointing to the next structure pointer
 *  @param [IN] fct:           a function that transforms a pointer to a list element to a string of characters
 *  @param [IN] fctarg:        fixed argument of the function
 *  @param [IN] before:        string (of characters) representing what must appear before the list
 *  @param [IN] prefix:        string (of characters) representing what must appear before each element
 *  @param [IN] separator:     string (of characters) that will be put between each element, but not at the end
 *                             or before the first
 *  @param [IN] after:         string (of characters) that will be put at the end of the list, after the last
 *                             element
 *
 *  If the function fct return NULL, the element is ignored
 *
 *  @return a string (of characters) written in arena with the list formed so.
 *
 *  Example: to create the list of expressions that is #define list of macros, transforming each element
 *            using both the name of the element and the number of parameters, use
 *          UTIL_DUMP_LIST(sa1, jdf->data, next, dump_data, sa2, "", "#define ", "\n", "\n"));
 */
#define UTIL_DUMP_LIST(arena, structure_ptr, nextfield, fct, fctarg, before, prefix, separator, after) \
    util_dump_list_fct( arena, structure_ptr,                           \
                        (char *)&(structure_ptr->nextfield)-(char *)structure_ptr, \
                        0, \
                        fct, fctarg, before, prefix, separator, after)

/**
 * util_dump_list_fct:
 *   function used by the UTIL_DUMP_LIST* macros. Do not use directly.
 */
static inline char*
util_dump_list_fct(string_arena_t *sa,
                   const void *firstelt, unsigned int next_offset, unsigned int elt_offset,
                   dumper_function_t fct, void *fctarg,
                   const char *before, const char *prefix, const char *separator, const char *after)
{
    char *eltstr;
    const char *prevstr = "";
    void *elt;

    string_arena_init(sa);

    string_arena_add_string(sa, "%s", before);

    while(firstelt != NULL) {
        elt = ((void **)((char*)(firstelt) + elt_offset));
        eltstr = fct(elt, fctarg);

        firstelt = *((void **)((char *)(firstelt) + next_offset));
        if( eltstr != NULL ) {
            string_arena_add_string(sa, "%s%s%s", prevstr, prefix, eltstr);
            prevstr = separator;
        }
    }

    string_arena_add_string(sa, "%s", after);

    return string_arena_get_string(sa);
}

jdf_def_list_t* jdf_create_properties_list( const char* name,
                                            int default_int,
                                            const char* default_char,
                                            jdf_def_list_t* next );

/**
 * Utilities to dump expressions and other parts of the internal
 * storage structure.
 */
typedef struct expr_info {
    struct string_arena* sa;
    const char* prefix;
    char*       assignments;
    int         nb_bound_locals;
    char**      bound_locals;
    const char* suffix;
} expr_info_t;

#define EMPTY_EXPR_INFO { .sa = NULL, .prefix = NULL, .assignments = NULL, .nb_bound_locals = 0, .bound_locals = NULL, .suffix = NULL }

/* The elem should be a jdf_expr_t while the arg should be an expr_info_t */
char * dump_expr(void **elem, void *arg);


#endif
