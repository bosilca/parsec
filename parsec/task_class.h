/*
 * Copyright (c) 2012-2019 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 */

#ifndef PARSEC_TASK_CLASS_H_HAS_BEEN_INCLUDED
#define PARSEC_TASK_CLASS_H_HAS_BEEN_INCLUDED

#include "parsec/parsec_config.h"
#include "parsec/interfaces/interface.h"
#include "parsec_internal.h"

// flow_array should be const, but it creates a warning in the generated code
bool parsec_helper_flow_is_in_flow_array(const parsec_flow_t *flow, parsec_flow_t *flow_array[], int flow_array_size);

void parsec_debug_dump_task_class_at_exec(parsec_task_class_t *tc);
void parsec_check_sanity_of_task_class(parsec_task_class_t *tc);

#endif  /* PARSEC_TASK_CLASS_H_HAS_BEEN_INCLUDED */
