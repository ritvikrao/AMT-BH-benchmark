#!/bin/bash
#
# Build Charm++ and the Barnes-Hut benchmark.
#
#   ./build.sh [extra make arguments...]
#
# This clones Charm++ into ./_deps/charm, builds the reconverse-<os>-<arch>
# target for this machine, and then builds `barnes` and the two input
# generators against it. Charm++ is the only dependency: Reconverse and LCI
# are fetched by Charm++'s own CMake, and the geometry headers this code needs
# are self-contained in this directory.
#
# The Reconverse targets are not on Charm++'s main branch yet, so the default
# branch below is the one that carries them, pinned to the commit the
# measurements were taken on (MULTINODE.md, "Decisions"). Reconverse is a git
# submodule of that tree and is checked out at the commit Charm++ records.
#
# LCI is overridden: Reconverse pins LCI ca88ce2c, whose bootstrap publishes
# P^2*D PMI keys and overruns Cray PMI2's quota past a handful of processes
# (README "Fabric environment"). dfb924cf is that pin plus the two bootstrap
# fixes.
#
# Environment:
#   CHARM_PATH    Use this already-built Charm++ target directory instead of
#                 building one.
#   CHARM_SRC     Where to clone Charm++.        (default ./_deps/charm)
#   CHARM_REPO    Charm++ git remote.
#   CHARM_BRANCH  Branch to clone.               (default charm4py-reconverse-fixes)
#   CHARM_COMMIT  Commit to build; empty for the branch tip.
#                                                (default bc6d7a283842)
#   LCI_TAG       LCI commit Reconverse fetches; empty for Reconverse's own pin.
#                                                (default dfb924cf3ee2)
#   CHARM_TARGET  Charm++ build triplet.         (default reconverse-<os>-<arch>)
#   CHARM_OPTS    Options passed to Charm++'s ./build.
#                 (default "--with-production --enable-tracing --disable-fortran")
#   JOBS          Parallel build jobs.           (default: core count)
#   TRACE         none|summary|projections.      (default none)
#
# The Charm++ tree is built once per commit and LCI tag and reused. To
# rebuild it from scratch -- after changing CHARM_TARGET or CHARM_OPTS, say --
# remove the build directory this prints, or remove ./_deps entirely.

set -euo pipefail
cd "$(dirname "$0")"

CHARM_SRC=${CHARM_SRC:-$PWD/_deps/charm}
CHARM_REPO=${CHARM_REPO:-https://github.com/charmplusplus/charm.git}
CHARM_BRANCH=${CHARM_BRANCH:-charm4py-reconverse-fixes}
CHARM_COMMIT=${CHARM_COMMIT-bc6d7a283842add92f78b99c5d1a25fab2c3e527}
LCI_TAG=${LCI_TAG-dfb924cf3ee25aece37b85473b41d406ad810252}
# No `smp` here: the Reconverse network type is inherently SMP and Charm++
# forces CMK_SMP itself. --disable-fortran because this benchmark is pure C++
# and never links the Fortran bindings, while Charm++ builds them by default
# -- which turns a missing or half-installed gfortran into a failed build for
# no gain. --enable-tracing because a production build otherwise compiles
# tracing out, and TRACE= below would then have nothing to link against; with
# no trace module linked the hooks cost nothing measurable.
CHARM_OPTS=${CHARM_OPTS:---with-production --enable-tracing --disable-fortran}
TRACE=${TRACE:-none}
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}

die() { echo "build.sh: $*" >&2; exit 1; }

# The Reconverse targets Charm++ carries; see src/arch in the Charm++ tree.
default_target() {
    local os arch
    case "$(uname -s)" in
        Darwin) os=darwin ;;
        Linux)  os=linux ;;
        *) die "no Reconverse target for $(uname -s); set CHARM_TARGET" ;;
    esac
    case "$(uname -m)" in
        arm64|aarch64) arch=arm8 ;;
        x86_64|amd64)  arch=x86_64 ;;
        *) die "no Reconverse target for $(uname -m); set CHARM_TARGET" ;;
    esac
    case "$os-$arch" in
        darwin-arm8|linux-arm8|linux-x86_64) ;;
        *) die "Charm++ has no reconverse-$os-$arch target; set CHARM_TARGET" ;;
    esac
    echo "reconverse-$os-$arch"
}

CHARM_TARGET=${CHARM_TARGET:-$(default_target)}

if [ -n "${CHARM_PATH:-}" ]; then
    [ -x "$CHARM_PATH/bin/charmc" ] ||
        die "no charmc under CHARM_PATH=$CHARM_PATH -- point it at a built target directory"
    CHARM_PATH=$(cd "$CHARM_PATH" && pwd)
    echo "==> using existing Charm++ at $CHARM_PATH"
else
    if [ ! -d "$CHARM_SRC" ]; then
        echo "==> cloning Charm++ ($CHARM_BRANCH) into $CHARM_SRC"
        # Blobless rather than shallow, so CHARM_COMMIT can be any commit on
        # the branch, not only its tip.
        git clone --filter=blob:none --branch "$CHARM_BRANCH" "$CHARM_REPO" "$CHARM_SRC"
    else
        echo "==> reusing Charm++ source at $CHARM_SRC"
    fi
    if [ -n "$CHARM_COMMIT" ]; then
        git -C "$CHARM_SRC" checkout -q "$CHARM_COMMIT" ||
            die "CHARM_COMMIT=$CHARM_COMMIT is not in $CHARM_SRC; git -C $CHARM_SRC fetch, or unset it"
    fi
    git -C "$CHARM_SRC" submodule update --init --recursive

    if [ -n "$LCI_TAG" ]; then
        # Word-split by Charm++'s ./build, so no spaces inside the value.
        CHARM_OPTS="$CHARM_OPTS --with-cmake-args=-DRECONVERSE_AUTOFETCH_LCI2_TAG=$LCI_TAG"
    fi

    # --destination fixes the build directory name. Without it Charm++ derives
    # it from the options, which would leave us guessing where charmc landed.
    # The commit and LCI tag are in the name, so changing either builds anew
    # instead of silently reusing a tree built from something else.
    rev=$(git -C "$CHARM_SRC" rev-parse --short=9 HEAD)
    dest=bh-$CHARM_TARGET-$rev${LCI_TAG:+-lci${LCI_TAG:0:8}}
    CHARM_PATH=$CHARM_SRC/$dest

    if [ -x "$CHARM_PATH/bin/charmc" ]; then
        echo "==> Charm++ already built at $CHARM_PATH"
    else
        echo "==> building Charm++ $CHARM_TARGET $CHARM_OPTS"
        (
            cd "$CHARM_SRC"
            # CHARM_OPTS is deliberately unquoted: it is a list of words.
            ./build charm++ "$CHARM_TARGET" $CHARM_OPTS \
                    --destination="$dest" -j"$JOBS"
        )
        [ -x "$CHARM_PATH/bin/charmc" ] ||
            die "Charm++ build finished but produced no $CHARM_PATH/bin/charmc"
    fi
    CHARM_PATH=$(cd "$CHARM_PATH" && pwd)
fi

echo "==> building barnes (TRACE=$TRACE)"
make CHARM_PATH="$CHARM_PATH" TRACE="$TRACE" -j"$JOBS" "$@"

# run.sh reads this so a plain `./run.sh` needs no environment of its own.
{
    echo "# Written by build.sh. Delete it to forget this build."
    printf 'CHARM_PATH=%q\n' "$CHARM_PATH"
    printf 'CHARM_TARGET=%q\n' "$CHARM_TARGET"
} > .build.env

echo
echo "Charm++:  $CHARM_PATH"
echo "binaries: barnes, plummer, gen"
echo "next:     ./run.sh"
