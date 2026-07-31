ARG BASE_IMAGE="invalid"

#-------------------------------------------------------------------------------
FROM ${BASE_IMAGE} AS base-image
#-------------------------------------------------------------------------------

# a colon ':' separated list of scripts to execute; including their result in the final image
ARG TEST_SCRIPTS="/container/extras/build_osu-micro-benchmarks.sh"

#-------------------------------------------------------------------------------
FROM base-image AS test
#-------------------------------------------------------------------------------
SHELL ["/bin/bash", "--login", "-c"]
RUN echo "Testing Image from ${BASE_IMAGE} ..." \
    && env \
    && cat /container/config_env.sh \
    && echo && echo && echo \
    && echo '----------------------------------------------------------------' \
    && echo "CC=${CC}" \
    && echo "CXX=${CXX}" \
    && echo "FC=${FC}" \
    && echo "F77=${F77}" \
    && echo \
    && echo "CFLAGS=${CFLAGS}" \
    && echo "CPPFLAGS=${CPPFLAGS}" \
    && echo "CXXFLAGS=${CXXFLAGS}" \
    && echo "FCFLAGS=${FCFLAGS}" \
    && echo "F77FLAGS=${F77FLAGS}" \
    && conda --version 2>/dev/null || echo " --> no conda in this container" \
    && which mpicc \
    && (mpicc --version 2>/dev/null || true) \
    && echo '----------------------------------------------------------------' \
    && docker-clean

RUN cat <<EOF >> /container/config_env.sh
# runtime-disable GPU-Aware MPI
## (OpenMPI)
export OMPI_MCA_opal_cuda_support=false
## (MPICH)
export MPIR_CVAR_ENABLE_GPU=0
EOF

RUN echo "Testing Image from ${BASE_IMAGE} ..." \
    && mpicxx -o ./report_placement /container/extras/report_placement.cxx -fopenmp \
    && report_cpu_features ./report_placement \
    && export OMP_NUM_THREADS=2 \
    && mpiexec -n 2 ./report_placement \
    && docker-clean

RUN echo "Testing Image from ${BASE_IMAGE} ..." \
    && ls /container/extras/*.sh \
    && SCRIPTS="${TEST_SCRIPTS}" \
    && export NRANKS="${MAKE_J_PROCS}" \
    && for script in ${SCRIPTS//:/ }; do \
        [ -x "${script}" ] \
            && echo "-------------------------------------------------------------------------------" \
            && echo "Executing Test Script ${script} ..." \
            && echo "-------------------------------------------------------------------------------" \
            && exec ${script} \
                || { echo "${script} not executable or does not exist??"; ls -l ${script}; continue; }; \
       done \
    && docker-clean

# Local Variables:
# mode: sh
# End:
