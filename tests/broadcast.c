/*
 * Copyright (c) 2009-2017 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 */

#include "parsec/parsec_config.h"
#include "parsec.h"
#include "parsec/execution_stream.h"
#include "parsec/arena.h"
#include "data_dist/matrix/two_dim_rectangle_cyclic.h"
#include "parsec/datatype.h"
#include <math.h>
#include "data_dist/matrix/broadcast_wrapper.h"
#include "parsec/data_internal.h"

int main( int argc, char* argv[] )
{
    parsec_context_t* parsec;
    int rc;
    parsec_taskpool_t* tp;
    two_dim_block_cyclic_t dcA;
    int cores = 1, world = 1, rank = 0;
    int mb = 1, nb = 1, lm = 1, ln = world;

#if defined(PARSEC_HAVE_MPI)
    {
        int provided;
        MPI_Init_thread(&argc, &argv, MPI_THREAD_SERIALIZED, &provided);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &world);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
#endif

    parsec = parsec_init(cores, &argc, &argv);

    ln = world;
    two_dim_block_cyclic_init( &dcA, matrix_Integer, matrix_Tile,
                               world, rank, mb, nb, lm, ln, 0, 0, lm, ln, 1, 1, 1 );
    dcA.mat = parsec_data_allocate((size_t)dcA.super.nb_local_tiles *
                                     (size_t)dcA.super.bsiz *
                                     (size_t)parsec_datadist_getsizeoftype(dcA.super.mtype));

    parsec_data_collection_set_key(&dcA.super.super, "A");

    parsec_data_collection_t *A = (parsec_data_collection_t *)&dcA;

    parsec_data_copy_t *gdata;
    parsec_data_t *data = NULL;
    int *real_data;

    //if(rank == A->rank_of_key(A, rank)) {
    if(rank == A->rank_of(A, 0, rank)) {
        data = A->data_of(A, 0, rank);

        gdata = data->device_copies[0];
        real_data = PARSEC_DATA_COPY_GET_PTR((parsec_data_copy_t *) gdata);
        *real_data = rank;  /* for the sake of having a valid value */
        if( rank == A->rank_of(A, 0, 0)) {
            *real_data = 20;
        }
    }
    assert(NULL != data);

    int32_t *ranks = calloc(world, sizeof(int32_t));
    int i;
    for(i = 0; i < world; i++) {
        ranks[i] = i+1;
    }

    tp = (parsec_taskpool_t*)parsec_broadcast_New(&data, rank, world,
                                                  A->rank_of(A, 0, 0), ranks, world,
                                                  NULL, MPI_INT, MPI_INT);

    rc = parsec_enqueue(parsec, tp);
    PARSEC_CHECK_ERROR(rc, "parsec_enqueue");

    rc = parsec_context_start(parsec);
    PARSEC_CHECK_ERROR(rc, "parsec_context_start");

    rc = parsec_context_wait(parsec);
    PARSEC_CHECK_ERROR(rc, "parsec_context_wait");

    if( rank != A->rank_of(A, 0, 0)) {
        gdata = data->device_copies[0];
        real_data = PARSEC_DATA_COPY_GET_PTR((parsec_data_copy_t *) gdata);
        printf("rank %d data %d (ptr %p)\n", rank, *real_data, real_data);
    }
    parsec_broadcast_Destruct( tp );

    parsec_fini(&parsec);

#if defined(PARSEC_HAVE_MPI)
    MPI_Finalize();
#endif  /* defined(PARSEC_HAVE_MPI) */

    return 0;
}
