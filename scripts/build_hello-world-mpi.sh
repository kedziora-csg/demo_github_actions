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
# Set HELLO_WORLD_MPI_RUN=0 to compile only -- used by the devenv Dockerfile,
# which bakes the binary in but should not launch MPI during `docker build`.

NRANKS="${NRANKS:-2}"
HELLO_WORLD_MPI_RUN="${HELLO_WORLD_MPI_RUN:-1}"

exe="${INSTALL_ROOT}/bin/hello_world_mpi"

# Prefer the copy next to this script; fall back to the in-container extras/.
src="${SCRIPTDIR}/hello_world_mpi.cxx"
[ -f "${src}" ] || src="/container/extras/hello_world_mpi.cxx"
[ -f "${src}" ] || { echo "cannot locate hello_world_mpi.cxx!!"; exit 1; }

MPICXX="${MPICXX:-mpicxx}"
which ${MPICXX} >/dev/null 2>&1 \
    || { echo "no ${MPICXX} found -- is this an MPI image?"; exit 1; }

# Probe for the OpenMP flag rather than assuming.  Every compiler family we
# currently build accepts -fopenmp (nvhpc aliases it), but -mp / -qopenmp are
# the native spellings for nvhpc / older Intel and cost nothing to check.
openmp_flag=""
probe="${STAGE_DIR}/hello_world_mpi_ompprobe.cxx"
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

set -x
${MPICXX} -o ${exe} ${src} ${openmp_flag} || exit 1
set +x

report_cpu_features ${exe} 2>/dev/null || ldd ${exe} || true

if [ "${HELLO_WORLD_MPI_RUN}" != "0" ]; then
    echo
    echo "Running ${exe} on ${NRANKS} rank(s), OMP_NUM_THREADS=${OMP_NUM_THREADS:-<unset>} ..."
    mpiexec -n ${NRANKS} ${mpiexec_args} ${exe}
fi
