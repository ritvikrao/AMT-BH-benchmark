#ifndef __DEFINES_H__
#define __DEFINES_H__

#include "limits.h"
#include "charm++.h"

#include "common.h"


#define TREE_KEY_BITS (sizeof(Key)*CHAR_BIT)

#define BITS_PER_DIM 21
#define BOXES_PER_DIM (1<<(BITS_PER_DIM))

#define NDIMS 3

// How much finer than one TreePiece's share the decomposition histogram is
// driven before the bins are grouped into TreePieces. A bin is the unit of
// error in that grouping -- a cut falls between bins, never inside one -- so
// this is what bounds the initial imbalance: with N/P particles wanted per
// TreePiece and bins of at most N/(P*DECOMP_OVERSAMPLE), no TreePiece is off
// its share by more than one bin.
//
// It buys evenness with time, and not because of the histogram: finer bins put
// the TreePiece boundaries deeper down the Morton curve, and the local tree
// then has to be refined further along each boundary before a node has a
// single owner. Measured on 100k Plummer bodies, 2016 TreePieces, worst
// TreePiece over the mean and seconds per step at 4 and 8 PEs:
//
//   oversample     1       2       4       8
//   max/mean    1.835   1.411   1.210   1.109
//   s/step 4PE  0.0625* 0.0655  0.0704  0.0758
//   s/step 8PE  0.0614* 0.0678  0.0733  0.0809
//
//   (*) one bin per TreePiece, which is what the old key-midpoint
//       decomposition amounted to.
//
// Per *PE* the choice hardly matters -- 504 TreePieces to a PE average out to
// 1.000 either way -- so this is paying for the evenness the specification
// asks for, not for throughput. 4 keeps the worst TreePiece within a quarter
// of its share for about a seventh of the step.
#define DECOMP_OVERSAMPLE 4
#define BUCKET_TOLERANCE 1.2

#define DIV2(x) ((x)>>1)
#define EVEN(x) ((DIV2(x)<<1)==(x))

#define BRANCH_FACTOR 2
#define LOG_BRANCH_FACTOR 1

const double opening_geometry_factor = 2 / sqrt(3.0);

#define COSMO_CONST(x) (x)
#define CONVERT_TO_COSMO_TYPE

#define RRDEBUG /* empty */

#define TB_DEBUG /* empty */

#define NUM_PRIORITY_BITS (sizeof(int)*CHAR_BIT)

#define REQUEST_MOMENTS_PRIORITY (-8)
#define RECV_MOMENTS_PRIORITY (-7)
#define REQUEST_NODE_PRIORITY (-6)
#define REQUEST_PARTICLES_PRIORITY (-5)
#define RECV_NODE_PRIORITY (-4)
#define RECV_PARTICLES_PRIORITY (-3)
#define REMOTE_GRAVITY_PRIORITY (-2)
#define LOCAL_GRAVITY_PRIORITY (-1)

#define REMOTE_NODE_REQUEST 9998
#define REMOTE_PARTICLE_REQUEST 9999

#endif
