#!/usr/bin/env bash

#-------------------------------------------------------------------------bh-
# Common Configuration Environment:

SCRIPTDIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
source ${SCRIPTDIR}/build_common.cfg \
    || source /container/extras/build_common.cfg \
    || { echo "cannot locate a suitable build_common.cfg!!"; exit 1; }
#-------------------------------------------------------------------------eh-

# Build the hybrid MPI+OpenMP placement diagnostic into ${INSTALL_ROOT}/bin,
# which is already on PATH inside the container, then smoke-test it.
#
# Set REPORT_PLACEMENT_RUN=0 to compile only -- used by the devenv Dockerfile,
# which bakes the binary in but should not launch MPI during `docker build`.

NRANKS="${NRANKS:-2}"
REPORT_PLACEMENT_RUN="${REPORT_PLACEMENT_RUN:-1}"

exe="${INSTALL_ROOT}/bin/report_placement"

# Prefer the copy next to this script; fall back to the in-container extras/.
src="${SCRIPTDIR}/report_placement.cxx"
[ -f "${src}" ] || src="/container/extras/report_placement.cxx"
[ -f "${src}" ] || { echo "cannot locate report_placement.cxx!!"; exit 1; }

MPICXX="${MPICXX:-mpicxx}"
which ${MPICXX} >/dev/null 2>&1 \
    || { echo "no ${MPICXX} found -- is this an MPI image?"; exit 1; }

# Probe for the OpenMP flag rather than assuming.  Every compiler family we
# currently build accepts -fopenmp (nvhpc aliases it), but -mp / -qopenmp are
# the native spellings for nvhpc / older Intel and cost nothing to check.
openmp_flag=""
probe="${STAGE_DIR}/report_placement_ompprobe.cxx"
cat <<'EOF' > ${probe}
#include <omp.h>
int main () { return omp_get_max_threads () > 0 ? 0 : 1; }
EOF
for flag in -fopenmp -mp -qopenmp; do
    if ${MPICXX} ${flag} -o ${probe}.exe ${probe} >/dev/null 2>&1; then
        openmp_flag="${flag}"
        break
    fi
done
rm -f ${probe} ${probe}.exe
[ -n "${openmp_flag}" ] \
    && echo "OpenMP flag: ${openmp_flag}" \
    || echo "WARNING: no working OpenMP flag found; building serial"

case "${MPI_FAMILY}" in
    "openmpi"*)
        # CI runners have 2-4 cores; 2 ranks x 2 threads needs oversubscription.
        mpiexec_args="--map-by :OVERSUBSCRIBE"
        ;;
    "mpich"*)
        export MPIR_CVAR_ENABLE_GPU=0
        mpiexec_args=""
        ;;
    *)
        mpiexec_args=""
        ;;
esac

mkdir -p ${INSTALL_ROOT}/bin

# MARCH_FLAGS is not optional here.  mpicxx is a wrapper, not a build system: it
# does not read $CXXFLAGS, so a flag exported into the image environment reaches
# this compile only if it is written on the command line.
#
# Leaving it off is not merely a lost optimisation.  nvc defaults to -tp native,
# i.e. the CPU of whichever runner happened to build this image, so an omitted
# flag bakes in that runner's instruction set -- and the binary then dies with
# SIGILL on any node that lacks it.  gcc and icpx default to generic x86-64 and
# hide the mistake.  MARCH_FLAGS already carries the right spelling for this
# compiler (-tp= for nvhpc, -march= elsewhere); do not second-guess it.
set -x
${MPICXX} -o ${exe} ${src} ${MARCH_FLAGS} ${openmp_flag} || exit 1
set +x

report_cpu_features ${exe} 2>/dev/null || ldd ${exe} || true

# Guard the above.  The smoke run below cannot catch an over-wide build -- it
# runs on the machine that compiled it, where the instructions are legal by
# construction.  Only inspecting the binary catches it, and only here, while it
# is cheap: the alternative is finding out on a compute node three hours into a
# queue.
# Any target that is NOT AVX-512-capable: the v1-v3 ABI levels, and the Zen 1-3
# cores.  (Zen 4 and x86-64-v4 do have it, so they are deliberately absent.)
case "${MARCH_FLAGS}" in
    *x86-64-v[123]*|*znver[123]*|*zen[123]*)
        if command -v objdump >/dev/null 2>&1 \
           && objdump -d "${exe}" 2>/dev/null | grep -q '%zmm'; then
            echo "ERROR: ${exe} uses AVX-512 (%zmm) despite MARCH_FLAGS=${MARCH_FLAGS}."
            echo "       The flag did not reach the compiler.  This binary would"
            echo "       SIGILL on any node without AVX-512."
            exit 1
        fi
        ;;
esac

if [ "${REPORT_PLACEMENT_RUN}" != "0" ]; then
    echo
    echo "Running ${exe} on ${NRANKS} rank(s), OMP_NUM_THREADS=${OMP_NUM_THREADS:-<unset>} ..."
    mpiexec -n ${NRANKS} ${mpiexec_args} ${exe}
fi
