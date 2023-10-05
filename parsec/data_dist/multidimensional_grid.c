/*
 * Copyright (c) 2010-2021 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 */
/************************************************************
 *distributed multidimensional_grid generation
 ************************************************************/

#include "parsec/parsec_config.h"
#include "parsec/parsec_internal.h"
#include "parsec/utils/debug.h"
#include "parsec/data.h"
#include "parsec/data_distribution.h"
#include "parsec/data_dist/multidimensional_grid.h"
#include "parsec/data_dist/matrix/two_dim_rectangle_cyclic.h"
#include "parsec/data_dist/matrix/sym_two_dim_rectangle_cyclic.h"
#include "parsec/data_dist/matrix/two_dim_tabular.h"

#if defined(PARSEC_HAVE_MPI)
#include <mpi.h>
#endif
#include <string.h>

#include "parsec/parsec_config.h"
#include "parsec/parsec_internal.h"
#include "parsec/utils/debug.h"
#include "parsec/data_dist/matrix/matrix.h"
#include "parsec/data_dist/matrix/two_dim_rectangle_cyclic.h"
#include "parsec/data_dist/matrix/matrix_internal.h"
#include "parsec/mca/device/device.h"
#include "parsec/vpmap.h"


static uint32_t MGRID_rank_of_coordinates(parsec_data_collection_t *desc, int tile_coordinates[((parsec_multidimensional_grid_t*)desc)->tiling_dimensions_number]);
void parsec_multidimensional_grid_key2coords(parsec_multidimensional_grid_t *dc,
                                                   parsec_data_key_t key,
                                                   int tile_coordinates[dc->tiling_dimensions_number]);

static parsec_data_key_t grid_data_key(struct parsec_data_collection_s *desc, ...);

static uint32_t MGRID_rank_of(parsec_data_collection_t *desc, ...);
static int32_t MGRID_vpid_of(parsec_data_collection_t* desc, ...);
static parsec_data_t* MGRID_data_of(parsec_data_collection_t* dc, ...);
static uint32_t MGRID_rank_of_key(parsec_data_collection_t* desc, parsec_data_key_t key);
static int32_t MGRID_vpid_of_key(parsec_data_collection_t* desc, parsec_data_key_t key);
static parsec_data_t* MGRID_data_of_key(parsec_data_collection_t* dc, parsec_data_key_t key);




static int MGRID_memory_register(parsec_data_collection_t* desc, parsec_device_module_t* device)
{
    parsec_multidimensional_grid_t * dc = (parsec_multidimensional_grid_t *)desc;
    if( (NULL == dc->grid ) || (dc->nb_local_tiles == 0)) {
        return PARSEC_SUCCESS;
    }

    return device->memory_register(device, desc,
                                   dc->grid,
                                   ((size_t)dc->tile_number_of_values *
                                   (size_t)parsec_datadist_getsizeoftype(dc->mtype)));
}

static int MGRID_memory_unregister(parsec_data_collection_t* desc, parsec_device_module_t* device)
{
    parsec_multidimensional_grid_t * dc = (parsec_multidimensional_grid_t *)desc;
    if( (NULL == dc->grid ) || (dc->nb_local_tiles == 0)) {
        return PARSEC_SUCCESS;
    }

    return device->memory_unregister(device, desc, dc->grid);
}

void parsec_multidimensional_grid_init(parsec_multidimensional_grid_t * dc,
                               parsec_matrix_type_t mtype,
                               int nodes, int myrank,
                               int tiling_dimensions_number, int tile_dimensions_number,
                               ...
                               // tiling_size [tiling_dimensions_number times],
                               // tile_size [tile_dimensions_number times],
                               // mpi_stencil_size [tiling_dimensions_number times],
                               
                               
                               /*
                               int mb,    int nb,   // Tile size
                               int lm,    int ln,   // Global matrix size (what is stored)
                               int i,     int j,    // Staring point in the global matrix
                               int m,     int n,    // Submatrix size (the one concerned by the computation
                               int P,     int Q,    // process process grid
                               int kp,    int kq,   // k-cyclicity
                               int ip,    int jq   // starting point on the process grid
                               */
                               )
{
	va_list ap;

	// fill in the dc structure
	dc->tiling_dimensions_number = tiling_dimensions_number;
	dc->tile_dimensions_number = tile_dimensions_number;

    va_start(ap, tiling_dimensions_number);
    for(int d=0;d<dc->tiling_dimensions_number;++d)
	{
		dc->tiling_size[d] = (int)va_arg(ap, unsigned int);
	}
    for(int d=0;d<dc->tile_dimensions_number;++d)
	{
		dc->tile_size[d] = (int)va_arg(ap, unsigned int);
	}
    for(int d=0;d<dc->tiling_dimensions_number;++d)
	{
		dc->mpi_stencil_size[d] = (int)va_arg(ap, unsigned int);
	}
    va_end(ap);

	dc->grid = NULL; // no data associated yet
	dc->mtype = mtype;

	dc->tile_number_of_values = 1;
	for(int d=0;d<dc->tile_dimensions_number;++d)
	{
		dc->tile_number_of_values *= dc->tile_size[d];
	}

	dc->tile_number = 1;
	for(int d=0;d<dc->tiling_dimensions_number;++d)
	{
		dc->tile_number *= dc->tiling_size[d];
	}

	dc->nb_local_tiles = 0;
	for(int t=0;t<dc->tile_number;++t)
	{
		int tile_coordinates[dc->tiling_dimensions_number];
		parsec_multidimensional_grid_key2coords(dc, t, tile_coordinates);
		int target_rank = MGRID_rank_of_coordinates((parsec_data_collection_t*)dc, tile_coordinates);
		if(target_rank == myrank)
		{
			++dc->nb_local_tiles;
		}
	}

	dc->data_map = (parsec_data_t**)calloc(dc->nb_local_tiles, sizeof(parsec_data_t*));
	dc->bsiz = dc->tile_number_of_values * parsec_datadist_getsizeoftype(dc->mtype);


	parsec_data_collection_t *o     = &(dc->super);

	parsec_data_collection_init( o, nodes, myrank );

	o->data_key = grid_data_key;

    /* set the methods */
    o->rank_of      = MGRID_rank_of;
    o->vpid_of      = MGRID_vpid_of;
    o->data_of      = MGRID_data_of;
    o->rank_of_key  = MGRID_rank_of_key;
    o->vpid_of_key  = MGRID_vpid_of_key;
    o->data_of_key  = MGRID_data_of_key;

    o->register_memory   = MGRID_memory_register;
    o->unregister_memory = MGRID_memory_unregister;


	/* finish to update the main object properties */
    /*o->key_to_string = tiled_matrix_key_to_string; // dont care atm YET
    if( asprintf(&(o->key_dim), "(%d, %d)", tdesc->lmt, tdesc->lnt) <= 0 ) {
        o->key_dim = NULL;
    }*/

    /* Define the default datatye of the datacollection */
    parsec_datatype_t elem_dt = PARSEC_DATATYPE_NULL;
    ptrdiff_t extent;
    parsec_translate_matrix_type( dc->mtype, &elem_dt );
    if( PARSEC_SUCCESS != parsec_matrix_define_datatype(&o->default_dtt, elem_dt,
                                              PARSEC_MATRIX_FULL, 1 /*diag*/,
                                              dc->tile_number_of_values, 1, dc->tile_number_of_values /*ld*/,
                                              -1/*resized*/, &extent)){
        parsec_fatal("Unable to create a datatype for the data collection.");
    }

/*
	// print the dc structure
	printf("dc.tiling_dimensions_number = %d\n", dc->tiling_dimensions_number);
	for(int d=0;d<dc->tiling_dimensions_number;++d)
	{
		printf("dc.tiling_size[%d] = %d\n", d, dc->tiling_size[d]);
	}
	for(int d=0;d<dc->tiling_dimensions_number;++d)
	{
		printf("dc.tile_size[%d] = %d\n", d, dc->tile_size[d]);
	}
	for(int d=0;d<dc->tiling_dimensions_number;++d)
	{
		printf("dc.mpi_stencil_size[%d] = %d\n", d, dc->mpi_stencil_size[d]);
	}
	printf("dc.tile_number_of_values = %d\n", dc->tile_number_of_values);
	printf("dc.tile_number = %d\n", dc->tile_number);
	printf("dc.nb_local_tiles = %d\n", dc->nb_local_tiles);
*/

}

void parsec_grid_destroy_data( parsec_multidimensional_grid_t *grid )
{
    if ( grid->data_map != NULL ) {
        parsec_data_t **data = grid->data_map;

        for(int i = 0; i < grid->nb_local_tiles; i++, data++) {
            if( NULL == *data ) continue;
            parsec_data_destroy( *data );
        }

        free( grid->data_map );
        grid->data_map = NULL;
    }
}

void parsec_grid_destroy( parsec_multidimensional_grid_t *grid )
{
    parsec_data_collection_t *dc = (parsec_data_collection_t*)grid;
    parsec_type_free(&dc->default_dtt);

    parsec_grid_destroy_data( grid );
    parsec_data_collection_destroy( dc );
}

static inline int MGRID_coordinates_to_position(parsec_multidimensional_grid_t *dc, int tile_coordinates[dc->tiling_dimensions_number])
{
	int position = 0;
	int offset_multiplier = 1;
	for(int d=0;d<dc->tiling_dimensions_number;++d)
	{
		position += tile_coordinates[d] * offset_multiplier;
		offset_multiplier *= dc->tiling_size[d];
	}

	//printf("coordinates %d %d %d -> position %d\n", tile_coordinates[0], tile_coordinates[1], tile_coordinates[2], position);

    return position;
}

/* return a unique key (unique only for the specified parsec_dc) associated to a data */
static parsec_data_key_t grid_data_key(struct parsec_data_collection_s *desc, ...)
{
    parsec_multidimensional_grid_t *dc = (parsec_multidimensional_grid_t *)desc;
    va_list ap;

	int tile_coordinates[dc->tiling_dimensions_number];

    /* Get coordinates */
    va_start(ap, desc);
    for(int d=0;d<dc->tiling_dimensions_number;++d)
	{
		tile_coordinates[d] = (int)va_arg(ap, unsigned int);
	}
    va_end(ap);

    int position = MGRID_coordinates_to_position(dc, tile_coordinates);

    return position;
}

void parsec_multidimensional_grid_key2coords(parsec_multidimensional_grid_t *dc,
                                                   parsec_data_key_t key,
                                                   int tile_coordinates[dc->tiling_dimensions_number])
{
	// compute the offset multiplier
	int offset_multiplier = 1;
	for(int d=0;d<dc->tiling_dimensions_number-1;++d)
	{
		offset_multiplier *= dc->tiling_size[d];
	}

	int d=dc->tiling_dimensions_number-1;
	do
	{
		tile_coordinates[d] = (key / offset_multiplier) % dc->tiling_size[d];
		if(d <= 0)
		{
			break;
		}
		--d;
		offset_multiplier /= dc->tiling_size[d];
	}while(true);
}

/*
 *
 * Set of functions with no k-cyclicity support
 *
 */

static uint32_t MGRID_rank_of_coordinates(parsec_data_collection_t *desc, int tile_coordinates[((parsec_multidimensional_grid_t*)desc)->tiling_dimensions_number])
{
	parsec_multidimensional_grid_t * dc = (parsec_multidimensional_grid_t *)desc;

	/* Assert using local info */
	for(int d=0;d<dc->tiling_dimensions_number;++d)
	{
		assert( tile_coordinates[d] < dc->tiling_size[d] );
	}

	int res = 0;
	//int process_coordinates[dc->tiling_dimensions_number];
	int offset_multiplier = 1;
	for(int d=0;d<dc->tiling_dimensions_number;++d)
	{
		// computes the process that should handle the tile depending on its cooresponding coordinates

		// coordinates of the tile inside the stencil
		const int local_coordinates = tile_coordinates[d] % dc->mpi_stencil_size[d];

		// coordinates of the process inside the stencil
		// the pattern is the following (in 2D):
		// 0 1 ... NX
		// NX+1 NX+2 ... 2NX
		// ...
		// (NY-1)*NX (NY-1)*NX+1 ... NY*NX
		res += offset_multiplier * local_coordinates;

		offset_multiplier *= dc->mpi_stencil_size[d];
	}

    return res;
}


static uint32_t MGRID_rank_of(parsec_data_collection_t *desc, ...)
{
	parsec_multidimensional_grid_t * dc = (parsec_multidimensional_grid_t *)desc;
	int tile_coordinates[dc->tiling_dimensions_number];

	va_list ap;
    dc = (parsec_multidimensional_grid_t *)desc;

	/* Get coordinates */
    va_start(ap, desc);
    for(int d=0;d<dc->tiling_dimensions_number;++d)
	{
		tile_coordinates[d] = (int)va_arg(ap, unsigned int);
	}
    va_end(ap);

    return MGRID_rank_of_coordinates(desc, tile_coordinates);
}

static uint32_t MGRID_rank_of_key(parsec_data_collection_t *desc, parsec_data_key_t key)
{
	parsec_multidimensional_grid_t * dc = (parsec_multidimensional_grid_t *)desc;
	int tile_coordinates[dc->tiling_dimensions_number];

    parsec_multidimensional_grid_key2coords(dc, key, tile_coordinates);

    return MGRID_rank_of(desc, tile_coordinates);
}

static int32_t MGRID_vpid_of_coordinates(parsec_data_collection_t *desc, int tile_coordinates[((parsec_multidimensional_grid_t *)desc)->tiling_dimensions_number])
{

	// TODO for the moment, vpid == rank
	return MGRID_rank_of_coordinates(desc, tile_coordinates);
}

static int32_t MGRID_vpid_of(parsec_data_collection_t *desc, ...)
{
    parsec_multidimensional_grid_t * dc;
    va_list ap;
    dc = (parsec_multidimensional_grid_t *)desc;

	int tile_coordinates[dc->tiling_dimensions_number];

    /* Get coordinates */
    va_start(ap, desc);
    for(int d=0;d<dc->tiling_dimensions_number;++d)
	{
		tile_coordinates[d] = (int)va_arg(ap, unsigned int);
	}
    va_end(ap);

    /* Assert using local info */
    for(int d=0;d<dc->tiling_dimensions_number;++d)
	{
		assert( tile_coordinates[d] < dc->tiling_size[d] );
	}

#if defined(DISTRIBUTED)
    assert(desc->myrank == MGRID_rank_of_coordinates(desc, tile_coordinates));
#endif

//printf("vpid of coordinates %d %d is %d\n", tile_coordinates[0], tile_coordinates[1], MGRID_vpid_of_coordinates(desc, tile_coordinates));

    return MGRID_vpid_of_coordinates(desc, tile_coordinates);
}

static int32_t MGRID_vpid_of_key(parsec_data_collection_t *desc, parsec_data_key_t key)
{
    parsec_multidimensional_grid_t *dc = (parsec_multidimensional_grid_t *)desc;
    int tile_coordinates[dc->tiling_dimensions_number];

    parsec_multidimensional_grid_key2coords(dc, key, tile_coordinates);

    return MGRID_vpid_of_coordinates(desc, tile_coordinates);
}

static parsec_data_t* MGRID_data_of(parsec_data_collection_t *desc, ...)
{
    int position;
    va_list ap;
    parsec_multidimensional_grid_t * dc;
    dc = (parsec_multidimensional_grid_t *)desc;

	int tile_coordinates[dc->tiling_dimensions_number]; // tile position in the whole grid

    /* Get coordinates */
    va_start(ap, desc);
	for(int d=0;d<dc->tiling_dimensions_number;++d)
	{
		tile_coordinates[d] = (int)va_arg(ap, unsigned int);
	}
    va_end(ap);

    /* Assert using local info */
	for(int d=0;d<dc->tiling_dimensions_number;++d)
	{
		assert( tile_coordinates[d] < dc->tiling_size[d] );
	}

#if defined(DISTRIBUTED)
    assert(desc->myrank == MGRID_rank_of_coordinates(desc, tile_coordinates));
#endif

    position = MGRID_coordinates_to_position(dc, tile_coordinates);

	int key = position;
	int pos = 0;

	if(dc->grid != NULL)
	{
		pos = position;
	}

	return parsec_data_create(dc->data_map + position,
						desc, key, (char*)dc->grid + pos * dc->bsiz,
						dc->bsiz,
						PARSEC_DATA_FLAG_PARSEC_MANAGED);
}

static parsec_data_t* MGRID_data_of_key(parsec_data_collection_t *desc, parsec_data_key_t key)
{
    parsec_multidimensional_grid_t *dc = (parsec_multidimensional_grid_t *)desc;

	int position = key;
	int pos = 0;

	if(dc->grid != NULL)
	{
		pos = position;
	}

    return parsec_data_create(dc->data_map + position,
						desc, key, (char*)dc->grid + pos * dc->bsiz,
						dc->bsiz,
						PARSEC_DATA_FLAG_PARSEC_MANAGED);
}

