#ifndef __DEFINES_H__
#define __DEFINES_H__

#include "limits.h"
#include "charm++.h"

#include "common.h"


#define TREE_KEY_BITS (sizeof(Key)*CHAR_BIT)

#define BITS_PER_DIM 21
// The universe box is grown by this fraction of its extent on each side, so
// no particle sits exactly on its greater corner (which would hash past the
// last Morton cell). Applied once to the global box, never per PE.
#define UNIVERSE_PAD 0.001
#define BOXES_PER_DIM (1<<(BITS_PER_DIM))

#define NDIMS 3

#define DECOMP_TOLERANCE 1.2
#define BUCKET_TOLERANCE 1.2

#define DIV2(x) ((x)>>1)
#define EVEN(x) ((DIV2(x)<<1)==(x))

// The tree's branching factor: 2 (binary, the default) or 8 (octree), set at
// build time with `make BRANCH=8`. The tree is part of the opening criterion,
// so the two compute different interactions: MULTINODE.md, "Tree shape".
#ifndef BRANCH_FACTOR
#define BRANCH_FACTOR 2
#endif
#if BRANCH_FACTOR == 2
#define LOG_BRANCH_FACTOR 1
#elif BRANCH_FACTOR == 8
#define LOG_BRANCH_FACTOR 3
#else
#error "BRANCH_FACTOR must be 2 or 8"
#endif

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
