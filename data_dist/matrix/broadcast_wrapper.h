/*
 * Copyright (c) 2014-2015 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 */

#ifndef _broadcast_wrapper_h
#define _broadcast_wrapper_h

#include "parsec.h"
#include "parsec/data.h"

parsec_taskpool_t*
parsec_broadcast_New(parsec_data_t **data, int32_t myrank, int32_t world,
                     int root,
                     const int32_t *ranks, int sz,
                     parsec_taskpool_t *master_tp,
                     parsec_datatype_t stype,
                     parsec_datatype_t rtype);


void parsec_broadcast_Destruct( parsec_taskpool_t *o );
#endif 
