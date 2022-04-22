/*
 * Copyright (c) 2010-2020 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 */

#ifndef PARSEC_DEVICE_CUDA_INTERNAL_H_HAS_BEEN_INCLUDED
#define PARSEC_DEVICE_CUDA_INTERNAL_H_HAS_BEEN_INCLUDED

#include "parsec/mca/device/cuda/device_cuda.h"

#if defined(PARSEC_HAVE_CUDA)

BEGIN_C_DECLS

/* From MCA parameters */
extern int parsec_device_cuda_enabled_index, parsec_device_cuda_enabled;
extern int parsec_cuda_sort_pending;
extern int parsec_cuda_memory_block_size, parsec_cuda_memory_percentage, parsec_cuda_memory_number_of_blocks;
extern char* parsec_cuda_lib_path;

PARSEC_DECLSPEC extern const parsec_device_module_t parsec_device_cuda_module;

int parsec_cuda_module_init( int device, parsec_device_module_t** module );
int parsec_cuda_module_fini(parsec_device_module_t* device);
int parsec_cuda_kernel_dequeue( parsec_execution_stream_t *es);
int parsec_cuda_kernel_enqueue( parsec_execution_stream_t *es,
                                parsec_task_t            *task,
                                int   starving_device_index);

END_C_DECLS

#endif /* defined(PARSEC_HAVE_CUDA) */

#endif  /* PARSEC_DEVICE_CUDA_INTERNAL_H_HAS_BEEN_INCLUDED */
