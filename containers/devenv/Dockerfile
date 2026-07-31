################################################################################
# Preliminaries
#
#  *** All ARGs are set to sensible defaults but generally overridden by ***
#  *** 'docker build --build-arg' command-line arguments.
#
# These build arguments control the flow of various layers.
#
#  FINAL_TARGET is the final FROM statement to build.
#
#  *_PREREQ defines the prerequisite step for a stage.
#    For example, The TOOLKITS_PREREQ could take on one of several values,
#    'miniforge', 'cuda/rocm', or even 'base_os'.  In this way the 'toolkits'
#    layer is dynamic and can include several (or none) packages.
#    Similarly, IOLIBS_PREREQ is generally 'mpi'; meaning the 'iolibs' layer
#    will be built after MPI with full MPI support; however it could be
#    'compilers' to build an image with no MPI support whatsoever. (rarely used)
#
################################################################################

ARG BASE_OS="almalinux9"

ARG COMPILER_FAMILY="os-gcc"
ARG MPI_FAMILY="openmpi"

ARG MINIFORGE_PREREQ="base_os"

ARG TOOLKITS_PREREQ="miniforge"

ARG IOLIBS_PREREQ="mpi"

ARG FFTLIBS_PREREQ="mpi-iolibs"

ARG FINAL_TARGET="fftlibs"



################################################################################
# Part 1: support for various Base Operating Systems
################################################################################

#-------------------------------------------------------------------------------
# Any of these upstream base images can be used as a ${BASE_OS}
#-------------------------------------------------------------------------------
FROM docker.io/almalinux:8 AS almalinux8
FROM docker.io/almalinux:9 AS almalinux9
FROM docker.io/almalinux:10 AS almalinux10

FROM rockylinux/rockylinux:8 AS rockylinux8
FROM rockylinux/rockylinux:9 AS rockylinux9
FROM rockylinux/rockylinux:10 AS rockylinux10

FROM docker.io/opensuse/leap:15 AS leap
FROM docker.io/opensuse/tumbleweed AS tumbleweed

FROM ubuntu:jammy AS jammy
FROM ubuntu:noble AS noble
FROM ubuntu:resolute AS resolute

# In theory we could not install CUDA or ROCM and instead take a
# preconfigured vendor image; leaving these here for reference.
# (since the vendors don't support the same array of OS images, usually this is not the approach taken)
FROM nvidia/cuda:12.9.1-cudnn-devel-ubuntu24.04 AS noble-cuda
FROM nvidia/cuda:12.9.1-cudnn-devel-rockylinux9 AS rockylinux9-cuda
FROM nvidia/cuda:12.9.1-cudnn-devel-rockylinux8 AS rockylinux8-cuda

FROM rocm/dev-almalinux-8:6.4.2-complete AS almalinux8-rocm
FROM rocm/dev-ubuntu-24.04:6.4.2-complete AS noble-rocm

#-------------------------------------------------------------------------------
FROM ${BASE_OS} AS base_os
#-------------------------------------------------------------------------------

# Use a bash login shell for all subsequent RUN commands. This will ultimately source the file
# /container/config_env.sh; which we build up incrementally; and defines the configuration of all
# the software installed in a particular image variant.
SHELL ["/bin/bash", "--login", "-o", "pipefail", "-c"]

# An OS-independent helper script for removing caches and temporary files to reduce layer size
RUN mkdir -p /container/{bin,extras,logs} && \
    <<FILE1   cat > /container/config_env.sh && \
    <<'FILE2' cat > /container/bin/docker-clean && \
    <<'FILE3' cat > /container/bin/list-large-directories && \
    <<'FILE4' cat > /container/bin/add_conf && \
    <<'FILE5' cat > /container/bin/strip-binaries &&\
    <<'FILE6' cat > /container/bin/remove-redundant-static-libs && \
    <<'FILE7' cat > /container/bin/remove-cuda-static-libs && \
    <<'FILE8' cat > /container/bin/report_cpu_features && \
    chmod a+rx /container/bin/* \
    && cat /container/config_env.sh \
    && ln -s /container/config_env.sh /etc/profile.d/z00-build-env.sh
#-------------------------------------------------------
# Container Environment Configuration
#  (source this script to configure installed software)
#-------------------------------------------------------
export PATH=$(echo "$PATH" | tr ':' '\n' | grep -v root | tr '\n' ':' | sed 's/:$//'):/container/bin
export CPATH=${CPATH}
export LIBRARY_PATH=${LIBRARY_PATH}
export LD_LIBRARY_PATH=${LD_LIBRARY_PATH}
export MANPATH=${MANPATH}

export MAKE_J_PROCS=\${MAKE_J_PROCS:-\$(nproc)}
TOGGLE_ENABLE_PARALLEL="--disable-parallel"
unset TOGGLE_WITH_CUDA
unset TOGGLE_WITH_ROCM
FILE1
#!/bin/bash
echo "[${0}]: cleaning temporary files from container image to reduce layer size"
[[ -x "$(command -v yum)" ]] && yum clean all
[[ -x "$(command -v apt)" ]] && apt clean
[[ -x "$(command -v zypper)" ]] && zypper clean -a
[[ -x "$(command -v conda)" ]] && conda clean --all --yes
for dir in /tmp/* /var/tmp/* /home/jupyter/{.ccache,.cache/pip,conda-bld,.conda} /root/* /root/.[^.]* /var/lib/apt/lists /var/log/* ; do
    [[ -e ${dir} ]] && rm -rf ${dir} 2>/dev/null || true
done
rm -rf /tmp/*
FILE2
#!/bin/bash

dirs="/home /mnt /tmp /opt /usr /var /container"
if [[ ${#} -ge 1 ]]; then
    dirs=${@}
fi

THRESHOLD_SIZE_MB=4

echo "[${0}]: Finding large files & directories >${THRESHOLD_SIZE_MB}MiB in ${dirs} - output is MiB"
for dir in ${dirs}; do
    dir=$(realpath ${dir})
    if [[ -d ${dir} ]]; then
        find 2>/dev/null ${dir} -type f -size +${THRESHOLD_SIZE_MB}M -print0 | xargs -0 --no-run-if-empty du --block-size=1MiB | sort -n
        du 2>/dev/null --block-size=1MiB --threshold=${THRESHOLD_SIZE_MB}MiB --separate-dirs ${dir} | sort -n
    fi
done
FILE3
#!/bin/bash

set -e

if [[ ${#} -ne 1 ]]; then
    echo "Usage: ${0} \"FOO=val\""
    exit 1
fi

echo -e "${1}" >> /container/config_env.sh
FILE4
#!/bin/bash

set -eu

if [[ ${#} -ne 1 ]]; then
    echo "Usage: ${0} <directory path>"
    exit 1
fi

dir=$(realpath ${1})

[[ -d "${dir}" ]] && echo "Stripping all binaries in ${dir}..." || { echo "ERROR: no such directory: ${dir}"; exit 1; }

TIMEFORMAT=$'--> Real time: %3R seconds'

echo "Before: $(du -hs ${dir})"

# ref: https://spack.readthedocs.io/en/latest/containers.html
time find -L ${dir} -type f -exec readlink -f '{}' \; | \
    xargs file -i | \
    grep 'charset=binary' | \
    grep 'x-executable\|x-archive\|x-sharedlib' | \
    awk -F: '{print $1}' | xargs --no-run-if-empty strip --strip-debug --preserve-dates

echo "After: $(du -hs ${dir})"
FILE5
#!/bin/bash

set -eu

if [[ ${#} -ne 1 ]]; then
    echo "Usage: ${0} <directory path>"
    exit 1
fi

dir=$(realpath ${1})

[[ -d "${dir}" ]] \
  && echo "[${BASH_SOURCE[0]}]: Removing redundant static libraries from ${dir}..." \
  || { echo "ERROR: no such directory: ${dir}"; exit 1; }

echo "Before: $(du -hs ${dir})"
while read -r static_lib; do

    if [[ ! -f "${static_lib}" ]]; then
        continue
    fi

    # Check typical libfoo.so / libfoo.a}
    shared_lib=${static_lib//'.a'/'.so'}
    shared_lib=$(realpath ${shared_lib})

    if cmp -s ${static_lib} ${shared_lib}; then
        # in case suffix match fails...
        continue
    elif [[ -f ${shared_lib} ]]; then
        shared_lib=$(realpath ${shared_lib})
        ls -lh ${static_lib} ${shared_lib}
        rm -vf ${static_lib}
    fi
done < <(find ${dir} -readable -type f -name "lib*.a")
echo "After: $(du -hs ${dir})"
FILE6
#!/bin/bash

set -eu

if [[ ${#} -ne 1 ]]; then
    echo "Usage: ${0} <directory path>"
    exit 1
fi

dir=$(realpath ${1})

[[ -d "${dir}" ]] \
  && echo "[${BASH_SOURCE[0]}]: Removing redundant CUDA toolkit static libraries from ${dir}..." \
  || { echo "ERROR: no such directory: ${dir}"; exit 1; }

echo "Before: $(du -hs ${dir})"

while read -r static_lib; do

    if [[ ! -f "${static_lib}" ]]; then
        continue
    fi

    # Check CUDA/NVHPC-style libcufoo.so / libcufoo_static.a}
    shared_lib=${static_lib}
    shared_lib=${shared_lib//'_static.a'/'.so'}
    shared_lib=${shared_lib//'_static_nocallback.a'/'.so'}
    shared_lib=$(realpath ${shared_lib})

    if cmp -s ${static_lib} ${shared_lib}; then
        # in case suffix match fails...
        continue
    elif [[ -f ${shared_lib} ]]; then
        shared_lib=$(realpath ${shared_lib})
        ls -lh ${static_lib} ${shared_lib}
        rm -vf ${static_lib}
    fi
done < <(find ${dir} -readable -type f -name "lib*_static*.a")
echo "After: $(du -hs ${dir})"
FILE7
#!/bin/bash
#-------------------------------------------------------------------------------
# report_cpu_features [executable]
#
#  (1) always: report this host's CPU and its microarchitecture (SIMD) features.
#  (2) optional: given an executable, also report the ISA it REQUIRES, so a
#      binary built for a newer CPU than the host (a latent SIGILL, e.g. an
#      nvhpc binary targeting -tp znver4/AVX-512 run on an AVX2-only runner)
#      is obvious instead of a bare "Illegal instruction".
#
#  Works on x86_64 and aarch64; purely informational, never fails the caller.
#-------------------------------------------------------------------------------
arch=$(uname -m)

echo "== host: $(uname -mns) =="
case "${arch}" in
    x86_64)
        grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ *//'
        echo -n "  ISA flags: "
        grep -m1 flags /proc/cpuinfo | grep -oE 'avx512[a-z]*|avx2|fma|bmi[12]|sse4_[12]' | sort -u | tr '\n' ' '
        echo
        ;;
    aarch64|arm64)
        grep -m1 -E 'CPU implementer|CPU part' /proc/cpuinfo
        echo -n "  SIMD: "
        grep -m1 'Features' /proc/cpuinfo | grep -oE 'asimd|sve[0-9]*|fphp|bf16|i8mm' | sort -u | tr '\n' ' '
        echo
        ;;
    *)
        echo "  (unhandled arch ${arch})"
        ;;
esac

bin="${1:-}"
[[ -z "${bin}" ]] && exit 0
if [[ ! -e "${bin}" ]]; then
    echo "report_cpu_features: no such file: ${bin}" >&2
    exit 0
fi

echo "== ${bin} requires =="
command -v ldd     >/dev/null 2>&1 && { ldd "${bin}" 2>/dev/null || true; }
command -v readelf >/dev/null 2>&1 \
    && { readelf -nW "${bin}" 2>/dev/null | grep -iE 'ISA|feature|properties' || echo "  (no GNU ISA-needed note)"; }

if command -v objdump >/dev/null 2>&1; then
    case "${arch}" in
        x86_64)
            if objdump -d "${bin}" 2>/dev/null | grep -qE '%zmm[0-9]+'; then
                echo "  AVX-512 (zmm) used by binary: YES  (requires an AVX-512 host)"
            else
                echo "  AVX-512 (zmm) used by binary: no"
            fi
            ;;
        aarch64|arm64)
            if objdump -d "${bin}" 2>/dev/null | grep -qE 'z[0-9]+\.[bhsd]'; then
                echo "  SVE (z-regs) used by binary: YES  (requires an SVE host)"
            else
                echo "  SVE (z-regs) used by binary: no"
            fi
            ;;
    esac
fi
exit 0
FILE8


#---------------------------------------------------------------------------------
# Initialize the base OS; whether it is a RHEL, SUSE, or Ubuntu variant to provide
# a common development environment

# we initialize /container/config_env.sh here via add_conf,
# setting variables like PATH etc. to whatever they are preconfigured from the OS.
# (We set rather than append at initialization; we will occasionally re-source this file repeatedly)
#
# Notes:
#  leap/GPG_TTY: sourcing /etc/bash.bashrc with no TTY can cause a failure,
#                particularly when running under "bash -elo pipefail"
#                so we simply comment-out the offending line below.
#---------------------------------------------------------------------------------
RUN exec &> >(tee /container/logs/base-os.log) \
    && cat /etc/*-release \
    && source /etc/os-release \
    && id_tag="|${PLATFORM_ID}|${VERSION_CODENAME}|${ID}|" \
    && echo -e "Installing Base OS Packages\n${id_tag}" \
    && case "${id_tag}" in \
        *"almalinux"*|*"|rocky|"*|*"|rhel|"*) \
                add_conf 'export PKG_INSTALL_CMD="dnf -y install"' \
                && yum -y install dnf dnf-plugins-core \
                && dnf -y install epel-release \
                && dnf group list \
                && dnf -y update \
                && sed -i 's/tsflags=nodocs/#tsflags=nodocs/g' /etc/yum.conf \
                && sed -i 's/tsflags=nodocs/#tsflags=nodocs/g' /etc/dnf/dnf.conf \
                && dnf -y install \
                       procps \
                       gcc{,-c++,-gfortran} gdb \
                       glibc-devel file pkgconf patch \
                       man man-pages man-db \
                       make autoconf automake libtool \
                       git wget which tar gzip bzip2 xz \
                       diffutils file rsync \
                       emacs-nox \
                       python3 \
                       cmake lld zlib-devel libzstd-devel xz-devel bzip2-devel lz4-devel \
                       libxml2-devel curl-devel libedit-devel \
                       sudo fakeroot \
            ;; \
        *"leap|"*|*"tumbleweed|"*) \
                add_conf 'export PKG_INSTALL_CMD="zypper --gpg-auto-import-keys --non-interactive install"' \
                && gcc_v=14 \
                && sed -i 's/rpm.install.excludedocs = yes/rpm.install.excludedocs = no/g' /etc/zypp/zypp.conf \
                && zypper --non-interactive refresh \
                && zypper --non-interactive update \
                && zypper --non-interactive install --force-resolution --allow-downgrade \
                          procps hostname \
                          gcc${gcc_v}{,-c++,-fortran} gdb \
                          glibc-devel file pkgconf patch \
                          man man-pages man-pages-posix \
                          gmake autoconf automake libtool \
                          git wget which tar gzip bzip2 xz \
                          awk diffutils file rsync \
                          emacs-nox \
                          python3 \
                          cmake lld zlib-devel libzstd-devel xz-devel libbz2-devel liblz4-devel \
                          libxml2-devel curl-devel libedit-devel \
                          sudo fakeroot \
                && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-${gcc_v} 50 \
                && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-${gcc_v} 50 \
                && update-alternatives --install /usr/bin/gfortran gfortran /usr/bin/gfortran-${gcc_v} 50 \
                && if [ -f /etc/bash.bashrc ]; then sed -i 's/GPG_TTY="`tty`"/#GPG_TTY="`tty`"/g' /etc/bash.bashrc; fi \
            ;; \
        *"|ubuntu|"*) \
                add_conf 'export PKG_INSTALL_CMD="apt-get install -y"' \
                && case "${id_tag}" in \
                    *"|jammy|"*) gcc_v=12 ;; \
                    *"|noble|"*) gcc_v=14 ;; \
                    *"|resolute|"*) gcc_v=14 ;; \
                   esac \
                && (echo -e 'y\n' | unminimize || true) \
                && apt-get update \
                && apt-get upgrade \
                && apt-get autoclean \
                && apt-get install -y \
                           procps \
                           {gcc,g++,gfortran}-${gcc_v} gdb \
                           libc-dev linux-libc-dev libc6-dev file rsync pkg-config patch \
                           man man-db manpages-dev manpages-posix manpages-posix-dev \
                           make autoconf automake libtool libtool-bin \
                           git curl wget tar gzip bzip2 xz-utils \
                           emacs-nox \
                           python3 \
                           cmake lld zlib1g-dev libzstd-dev liblzma-dev libbz2-dev liblz4-dev \
                           libxml2-dev libcurl4-openssl-dev libedit-dev \
                           sudo fakeroot \
                && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-${gcc_v} 50 \
                && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-${gcc_v} 50 \
                && update-alternatives --install /usr/bin/gfortran gfortran /usr/bin/gfortran-${gcc_v} 50 \
            ;; \
        *) \
            echo "Unrecognized base operating system!!" \
                && echo "id_tag=${id_tag}" \
                && exit 1 \
            ;; \
       esac \
    && docker-clean


################################################################################
# Part 2: Toolkits and 3rd party libraries that do not need to be compiled.
################################################################################

#-------------------------------------------------------------------------------
# CUDA stages - runfile installer
# about stubs & LD_LIBRARY_PATH:
#   on a host with no NVIDIA device, libcuda.so etc will be provided by the stubs.
#   these need to be overloaded at runtime by a proper libcuda.so which necessarily
#   comes from the host.  So we put the system locations - /usr/lib64 or /usr/lib,
#   into the LD_LIBRARY_PATH ahead of the stubs.  And we use ldconfig to resolve the stubs
#   stubs as last resort.  This allows us to link and run on non-GPU hosts; and the
#   LD_LIBRARY_PATH precedence should find the host libcuda.so should there be one.
FROM base_os AS cuda-runfile
#-------------------------------------------------------------------------------
ARG CUDA_VERSION="12.9"
ARG CUDA_INSTALLER_URL="https://developer.download.nvidia.com/compute/cuda/12.9.1/local_installers/cuda_12.9.1_575.57.08_linux.run"
ARG CUDA_HOME="/container/cuda/${CUDA_VERSION}"
ENV CUDA_HOME="${CUDA_HOME}"
RUN exec &> >(tee /container/logs/cuda.log) \
    && cd /tmp \
    && env \
    && echo "Cuda v${CUDA_VERSION} - runfile installer" \
    && echo "CUDA: https://docs.nvidia.com/cuda/eula/index.html" >> /container/EULAs.txt \
    && mkdir -p ${CUDA_HOME} \
    && curl --retry 3 --retry-delay 5 -sO ${CUDA_INSTALLER_URL} \
    && bash ./cuda_*_linux*.run \
            --silent \
            --toolkit \
            --toolkitpath=${CUDA_HOME} \
    && rm -f ./cuda_*_linux*.run \
    && cd ${CUDA_HOME} \
    && rm_paths="nsight-*/ extras/ bin/nsys* bin/nsight* libnvvp/ nsightee_plugins/" \
    && echo "Removing extra bloat: ${rm_paths}" > README.whered_stuff_go && (du -hs ${rm_paths} >> README.whered_stuff_go || true) \
    && rm -rf ${rm_paths} \
    && remove-cuda-static-libs ${CUDA_HOME} >> README.whered_stuff_go \
    && cat README.whered_stuff_go \
    && find ${CUDA_HOME} ! -readable -print0 | xargs -0 --no-run-if-empty rm -vf \
    && add_conf "\n# CUDA ${CUDA_VERSION}" \
    && add_conf "export CUDA_VERSION=${CUDA_VERSION}" \
    && add_conf "export CUDA_HOME=${CUDA_HOME}" \
    && add_conf "export PATH=${CUDA_HOME}/bin:\${PATH}" \
    && add_conf "export CPATH=${CUDA_HOME}/include:\${CPATH}" \
    && add_conf "export CUDA_STUBDIR=$(find ${CUDA_HOME} -type d -name stubs)" \
    && add_conf "export LIBRARY_PATH=${CUDA_HOME}/lib64:\${CUDA_STUBDIR}:\${LIBRARY_PATH}" \
    && add_conf "export LD_LIBRARY_PATH=/usr/lib64:/usr/lib:\${LD_LIBRARY_PATH}" \
    && add_conf "export NVCC_PREPEND_FLAGS=\"-arch=all-major --cudart shared -allow-unsupported-compiler -Wno-deprecated-gpu-targets\"" \
    && add_conf "TOGGLE_WITH_CUDA=\"--with-cuda=${CUDA_HOME}\"" \
    && source /container/config_env.sh \
    && echo "${CUDA_STUBDIR}" >> /etc/ld.so.conf.d/cuda-stubs.conf \
    && ldconfig --verbose \
    && docker-clean

LABEL cuda="${CUDA_VERSION}"

#-------------------------------------------------------------------------------
# CUDA stages - base image
FROM base_os AS cuda-baseimage
#-------------------------------------------------------------------------------
RUN exec &> >(tee /container/logs/cuda.log) \
    && CUDA_HOME=$(ls -d /usr/local/cuda-??.?) \
    && CUDA_MAJOR_MINOR=${CUDA_HOME//'/usr/local/cuda-'/} \
    && echo "Cuda v${CUDA_VERSION} - from base image" \
    && echo "CUDA: https://docs.nvidia.com/cuda/eula/index.html" >> /container/EULAs.txt \
    && add_conf "\n# CUDA ${CUDA_MAJOR_MINOR}" \
    && add_conf "export CUDA_VERSION=${CUDA_MAJOR_MINOR}" \
    && add_conf "export CUDA_HOME=${CUDA_HOME}" \
    && add_conf "export PATH=${CUDA_HOME}/bin:\${PATH}" \
    && add_conf "export CPATH=${CUDA_HOME}/include:\${CPATH}" \
    && add_conf "export CUDA_STUBDIR=$(find ${CUDA_HOME} -type d -name stubs)" \
    && add_conf "export LIBRARY_PATH=${CUDA_HOME}/lib64:\${CUDA_STUBDIR}:\${LIBRARY_PATH}" \
    && add_conf "export NVCC_PREPEND_FLAGS=\"-arch=all-major --cudart shared -allow-unsupported-compiler -Wno-deprecated-gpu-targets\"" \
    && source /container/config_env.sh \
    && add_conf "TOGGLE_WITH_CUDA=\"--with-cuda=${CUDA_HOME} --with-cuda-libdir=${CUDA_STUBDIR}\"" \
    && echo "${CUDA_STUBDIR}" >> /etc/ld.so.conf.d/cuda-stubs.conf \
    && ldconfig --verbose \
    && docker-clean

LABEL cuda="${CUDA_VERSION}"

#-------------------------------------------------------------------------------
# CUDA meta stage-select either cuda-runfile or cuda-baseimage
FROM cuda-runfile AS cuda
#-------------------------------------------------------------------------------



#-------------------------------------------------------------------------------
# ROCm stages
FROM base_os AS rocm
#-------------------------------------------------------------------------------
ARG ROCM_VERSION="6.2.3"
ARG ROCM_HOME="/opt/rocm-${ROCM_VERSION}"
ENV ROCM_HOME="${ROCM_HOME}"
RUN exec &> >(tee /container/logs/rocm.log) \
    && echo "ROCm - amdgpu-install installer (https://rocm.docs.amd.com/projects/install-on-linux/en/latest/install/amdgpu-install.html)" \
    && echo "ROCm: https://rocm.docs.amd.com/en/latest/about/license.html" >> /container/EULAs.txt \
    && source /etc/os-release \
    && cd /tmp \
    && env \
    && mkdir -p ${ROCM_HOME:-/container/rocm} \
    && case "|${PLATFORM_ID}|${VERSION_CODENAME}|${ID}|" in \
        *"|platform:el8|"*) \
            ${PKG_INSTALL_CMD} https://repo.radeon.com/amdgpu/${ROCM_VERSION}/el/8.10/main/x86_64/amdgpu-install-6.2.60203-2044426.el8.noarch.rpm \
            ;; \
        *"|platform:el9|"*) \
            ${PKG_INSTALL_CMD} https://repo.radeon.com/amdgpu/${ROCM_VERSION}/el/9.4/main/x86_64/amdgpu-install-6.2.60203-2044426.el9.noarch.rpm \
            ;; \
        *"|leap|"*) \
            echo -e "gpgcheck = off\nrepo_gpgcheck = off\npkg_gpgcheck = off" >> /etc/zypp/zypp.conf \
            && zypper --gpg-auto-import-keys --no-gpg-checks --non-interactive \install https://repo.radeon.com/amdgpu/${ROCM_VERSION}/sle/15.6/main/x86_64/amdgpu-install-6.2.60203-2044426.noarch.rpm \
            && zypper --gpg-auto-import-keys --no-gpg-checks --non-interactive update \
            ;; \
        *"|ubuntu|"*) \
            curl --retry 3 --retry-delay 5 -sO https://repo.radeon.com/amdgpu-install/${ROCM_VERSION}/ubuntu/noble/amdgpu-install_6.2.60203-1_all.deb \
            && ${PKG_INSTALL_CMD} ./amdgpu-install*.deb \
            ;; \
        *) \
            exit 1 \
            ;; \
       esac \
    && amdgpu-install --help \
    && amdgpu-install --list-usecase \
    && amdgpu-install -y --usecase=hiplibsdk --no-dkms --accept-eula --dryrun \
    && amdgpu-install -y --usecase=hiplibsdk --no-dkms --accept-eula \
    && add_conf "\n# ROCm ${ROCM_VERSION}" \
    && add_conf "export ROCM_VERSION=${ROCM_VERSION}" \
    && add_conf "export PATH=${ROCM_HOME}/bin:\${PATH}" \
    && add_conf "export CPATH=${ROCM_HOME}/include:\${CPATH}" \
    && add_conf "export LIBRARY_PATH=${ROCM_HOME}/lib:\${LIBRARY_PATH}" \
    && add_conf "TOGGLE_WITH_ROCM=\"--with-rocm=${ROCM_HOME} --with-hip=${ROCM_HOME}\"" \
    && echo "${ROCM_HOME}/lib" >> /etc/ld.so.conf.d/rocm-${ROCM_VERSION}.conf \
    && ldconfig --verbose \
    && docker-clean

LABEL rocm="${ROCM_VERSION}"



#-------------------------------------------------------------------------------
# miniforge stage
# MINIFORGE_PREREQ could be cuda/rocm, or base_os
FROM ${MINIFORGE_PREREQ} AS miniforge
# clean with the --force-pkgs-dirs option now, while we know there are no dangling
# symbolic links.  Because, later, conda clean --all will not get these.
#   -f, --force-pkgs-dirs
#       Remove *all* writable package caches. This option is not included with the
#       --all flag. WARNING: This will break environments with packages installed
#       using symlinks back to the package cache.
#-------------------------------------------------------------------------------
RUN exec &> >(tee /container/logs/miniforge.log) \
    && mkdir -p /container/miniforge \
    && env \
    && cd /tmp && wget --quiet "https://github.com/conda-forge/miniforge/releases/latest/download/Miniforge3-$(uname)-$(uname -m).sh" \
    && bash ./Miniforge3-$(uname)-$(uname -m).sh -b -u -p /container/miniforge \
    && ln -sf /container/miniforge/etc/profile.d/conda.sh /container/init-conda.sh \
    && add_conf "\n# Miniforge" \
    && add_conf "source /container/miniforge/etc/profile.d/conda.sh" \
    && source /container/init-conda.sh \
    && conda --version \
    && conda clean --all --force-pkgs-dirs --yes --verbose \
    && docker-clean



#-------------------------------------------------------------------------------
# TOOLKITS_PREREQ could be miniforge, cuda/rocm, or even base_os
FROM ${TOOLKITS_PREREQ} AS toolkits
RUN echo "Setting the third-party toolkits layer"
#-------------------------------------------------------------------------------



# ################################################################################
# # Part 3: Compilers
# ################################################################################

#-------------------------------------------------------------------------------
# GCC compilers stage (taken from base OS installation)
FROM toolkits AS os-gcc
RUN echo "Selecting base OS gcc compilers" \
    && add_conf "\n# GCC compilers libraries (base OS)" \
    && add_conf "export COMPILER_FAMILY=gcc" \
    && add_conf "export GCC_VERSION=$(gcc -dumpfullversion)" \
    && add_conf "export gcc_CC=$(which gcc) && export CC=\${gcc_CC}" \
    && add_conf "export gcc_CXX=$(which g++) && export CXX=\${gcc_CXX}" \
    && add_conf "export gcc_F77=$(which gfortran) && export F77=\${gcc_F77}" \
    && add_conf "export gcc_FC=$(which gfortran) && export FC=\${gcc_FC}" \
    && docker-clean
LABEL compiler="gcc"
LABEL gcc="os-gcc"



#-------------------------------------------------------------------------------
# GCC compilers stage
# Note, if installing CUDA, use a supported GCC!:
#   https://docs.nvidia.com/cuda/cuda-installation-guide-linux/index.html#host-compiler-support-policy
# ("unset LIBRARY_PATH CPATH C_INCLUDE_PATH PKG_CONFIG_PATH" fixes this on AWS/docker:)
#   g++: error: spec failure: unrecognized spec option '@'
#   g++: fatal error: unknown spec function 'dumps'
# About gcc & LD_LIBRARY_PATH:
#   we might be installing a gcc older than that on the host.  If so; we do not want the
#   installed gcc taking precedence for libstdc++.so; lest we get undefined symbols and break the
#   host tools. So we only add this compiled gcc's libraries to the search paths if it is newer
#   than the host version.
# mirrors:
#  https://gcc.gnu.org/mirrors.html
FROM toolkits AS gcc
#-------------------------------------------------------------------------------
ARG GCC_VERSION="13.3.0"
RUN exec &> >(tee /container/logs/gcc.log) \
    && echo "Building gcc-${GCC_VERSION}" \
    && unset LIBRARY_PATH CPATH C_INCLUDE_PATH PKG_CONFIG_PATH \
    && env \
    && GCC_INSTALL_PATH="/container/gcc/${GCC_VERSION}" \
    && (cd /tmp \
            && curl --retry 3 --retry-delay 5 -sSL https://ftpmirror.gnu.org/gnu/gcc/gcc-${GCC_VERSION}/gcc-${GCC_VERSION}.tar.xz | tar Jx \
                || curl --retry 3 --retry-delay 5 -sSL https://mirrorservice.org/sites/sourceware.org/pub/gcc/releases/gcc-${GCC_VERSION}/gcc-${GCC_VERSION}.tar.xz | tar Jx \
                || curl --retry 3 --retry-delay 5 -sSL https://ftp.fu-berlin.de/unix/languages/gcc/releases/gcc-${GCC_VERSION}/gcc-${GCC_VERSION}.tar.xz | tar Jx) \
    && cd /tmp/gcc-${GCC_VERSION} \
    && ./configure --help \
    && ./contrib/download_prerequisites \
    && mkdir /tmp/gcc-build && cd /tmp/gcc-build \
    && (/tmp/gcc-${GCC_VERSION}/configure \
                 --prefix=${GCC_INSTALL_PATH} \
                 --disable-static --enable-shared \
                 --enable-languages=c,c++,fortran \
                 --disable-bootstrap \
                 --disable-multilib \
            || cat config.log) \
    && make --no-print-directory --jobs ${MAKE_J_PROCS} V=0 \
    && make --no-print-directory --silent install-strip \
    && add_conf "\n# GCC compilers" \
    && add_conf "export COMPILER_FAMILY=gcc" \
    && add_conf "export GCC_VERSION=${GCC_VERSION}" \
    && add_conf "export PATH=/container/gcc/${GCC_VERSION}/bin:\${PATH}" \
    && if [ "$(${GCC_INSTALL_PATH}/bin/gcc -dumpfullversion | cut -d'.' -f1)" -gt "$(gcc -dumpfullversion | cut -d'.' -f1) " ]; then \
         add_conf "export LD_LIBRARY_PATH=/container/gcc/${GCC_VERSION}/lib64:/container/gcc/${GCC_VERSION}/lib:\${LD_LIBRARY_PATH}" ;\
         echo "${GCC_INSTALL_PATH}/lib" >> /etc/ld.so.conf.d/gcc-${GCC_VERSION}.conf ;\
         echo "${GCC_INSTALL_PATH}/lib64" >> /etc/ld.so.conf.d/gcc-${GCC_VERSION}.conf ;\
         ldconfig --verbose ;\
       fi \
    && source /container/config_env.sh \
    && add_conf "export gcc_CC=$(which gcc) && export CC=\${gcc_CC}" \
    && add_conf "export gcc_CXX=$(which g++) && export CXX=\${gcc_CXX}" \
    && add_conf "export gcc_F77=$(which gfortran) && export F77=\${gcc_F77}" \
    && add_conf "export gcc_FC=$(which gfortran) && export FC=\${gcc_FC}" \
    && docker-clean
LABEL compiler="gcc"
LABEL gcc="${GCC_VERSION}"



#-------------------------------------------------------------------------------
# Intel OneAPI stage
FROM toolkits AS oneapi
#-------------------------------------------------------------------------------
# https://packages.spack.io/package.html?name=intel-oneapi-compilers
# https://github.com/spack/spack/blob/develop/var/spack/repos/builtin/packages/intel-oneapi-compilers/package.py
# Intel compilers do not -rpath their own libraries, relying instead on LD_LIBRARY_PATH
# We'd rather not need that, so add to the system search path
#
# Install and remove C++, Fortran one at a time as we go for when disk space is tight, rather than downloading both installers at once.
ARG ONEAPI_VERSION=2026.0.0
ARG ONEAPI_CC_URL=https://registrationcenter-download.intel.com/akdlm/IRC_NAS/e2f4cda3-8891-4d0e-bf60-00d19c4e3e27/intel-dpcpp-cpp-compiler-2026.0.0.564_offline.sh
ARG ONEAPI_FC_URL=https://registrationcenter-download.intel.com/akdlm/IRC_NAS/176ca159-bee8-44f2-9164-db26f95de382/intel-fortran-compiler-2026.0.0.573_offline.sh
RUN exec &> >(tee /container/logs/oneapi.log) \
    && echo "Installing Intel-OneAPI" \
    && echo "Intel OneAPI: https://www.intel.com/content/www/us/en/content-details/777700/intel-end-user-license-agreement-for-developer-tools-august-2024.html" >> /container/EULAs.txt \
    && ONEAPI_INSTALL_PATH="/container/intel-oneapi" \
    && mkdir -p /tmp/intel_installers && cd /tmp/intel_installers \
    && installer_opts="-a -s --eula accept --install-dir ${ONEAPI_INSTALL_PATH} --ignore-errors" \
    && curl --retry 3 --retry-delay 5 -s ${ONEAPI_CC_URL} -o cpp-installer.sh && ls -ltrh cpp-installer.sh && chmod +x ./cpp-installer.sh && ./cpp-installer.sh ${installer_opts} && rm -rf cpp-installer.sh \
    && curl --retry 3 --retry-delay 5 -s ${ONEAPI_FC_URL} -o fortran-installer.sh && ls -ltrh fortran-installer.sh && chmod +x ./fortran-installer.sh && ./fortran-installer.sh ${installer_opts} && rm -rf fortran-installer.sh \
    && cd ${ONEAPI_INSTALL_PATH} \
    && rm_paths="mpi debugger conda_channel compiler/*/linux/lib/oclfpga /opt/intel" \
    && echo "Removing extra bloat: ${rm_paths}" > README.whered_stuff_go && (du -hs ${rm_paths} >> README.whered_stuff_go || true) && cat README.whered_stuff_go \
    && rm -rf ${rm_paths} /tmp/intel_installers /var/intel \
    && add_conf "\n# Intel OneAPI compilers" \
    && add_conf "export COMPILER_FAMILY=oneapi" \
    && add_conf "export ONEAPI_VERSION=${ONEAPI_VERSION}" \
    && add_conf "source ${ONEAPI_INSTALL_PATH}/setvars.sh --force >/dev/null 2>&1" \
    && source /container/config_env.sh \
    && add_conf "export oneapi_CC=$(which icx) && export CC=\${oneapi_CC}" \
    && add_conf "export oneapi_CXX=$(which icpx) && export CXX=\${oneapi_CXX}" \
    && add_conf "export oneapi_F77=$(which ifx) && export F77=\${oneapi_F77}" \
    && add_conf "export oneapi_FC=$(which ifx) && export FC=\${oneapi_FC}" \
    && echo ${LD_LIBRARY_PATH} | tr ':' '\n' | grep ${ONEAPI_INSTALL_PATH} >> /etc/ld.so.conf.d/intel-oneapi.conf \
    && ldconfig --verbose \
    && docker-clean
LABEL compiler="oneapi"
LABEL oneapi="${ONEAPI_VERSION}"



#-------------------------------------------------------------------------------
# AMD AOCC stage
FROM toolkits AS aocc
#-------------------------------------------------------------------------------
# https://github.com/spack/spack/blob/develop/var/spack/repos/builtin/packages/aocc/package.py
ARG AOCC_VERSION="5.2.0"
ARG AOCC_URL="https://download.amd.com/developer/eula/aocc/aocc-5-2/aocc-compiler-5.2.0.tar"
RUN exec &> >(tee /container/logs/aocc.log) \
    && echo "Installing AMD AOCC-${AOCC_VERSION}" \
    && echo "AMD AOCC: https://www.amd.com/en/developer/aocc/aocc-compiler/eula.html" >> /container/EULAs.txt \
    && AOCC_INSTALL_PATH="/container/aocc/${AOCC_VERSION}" \
    && mkdir -p ${AOCC_INSTALL_PATH} && cd ${AOCC_INSTALL_PATH} \
    && curl --retry 3 --retry-delay 5 -sSL ${AOCC_URL} | tar Jx \
    && mv aocc-compiler-${AOCC_VERSION}/* . && rmdir aocc-compiler-${AOCC_VERSION} \
    && ls -ltrh \
    && ./install.sh && mv ../setenv_AOCC.sh . \
    && add_conf "\n# AMD AOCC compilers" \
    && add_conf "export COMPILER_FAMILY=aocc" \
    && add_conf "export AOCC_VERSION=${AOCC_VERSION}" \
    && add_conf "source ${AOCC_INSTALL_PATH}/setenv_AOCC.sh >/dev/null 2>&1" \
    && source /container/config_env.sh \
    && add_conf "export aocc_CC=$(which clang) && export CC=\${aocc_CC}" \
    && add_conf "export aocc_CXX=$(which clang++) && export CXX=\${aocc_CXX}" \
    && add_conf "export aocc_F77=$(which flang) && export F77=\${aocc_F77}" \
    && add_conf "export aocc_FC=$(which flang) && export FC=\${aocc_FC}" \
    && echo ${LD_LIBRARY_PATH} | tr ':' '\n' | grep ${AOCC_INSTALL_PATH} >> /etc/ld.so.conf.d/aocc.conf \
    && ldconfig --verbose \
    && docker-clean
LABEL compiler="aocc"
LABEL aocc="${AOCC_VERSION}"



#-------------------------------------------------------------------------------
# NVHPC stage
FROM toolkits AS nvhpc
# Notes: math_libs, cuda, etc...
#   the NVHPC directories cuda/XX.Y and math_libs/XX.Y contain bits of the CUDA
#   toolkit - which we may have already installed.
#   If so, remove the duplicates and symlink from ${CUDA_HOME}.
#   (Note for now we assume a match when XX.Y are the same; alternatively we compare the files)
#
#   When using the cuda_multi versions; these are LARGE; in addition to removing cruft
#   after the fact; filter some things at extract time - like the CUDA we don't want.
#   (especially important for space constrained GHA runners).
#-------------------------------------------------------------------------------
# 24.x and previous do not fPIC by default; which cause all sorts of problems
# when building shared libraries later. (not the case for the other compilers.)
ARG NVHPC_VERSION="25.1"
ARG NVHPC_URL="https://developer.download.nvidia.com/hpc-sdk/25.1/nvhpc_2025_251_Linux_x86_64_cuda_12.6.tar.gz"
ARG MARCH_FLAGS=""
RUN exec &> >(tee /container/logs/nvhpc.log) \
    && echo "Installing NVIDIA HPC Toolkit ${NVHPC_VERSION}" \
    && echo "NVHPC: https://docs.nvidia.com/hpc-sdk/eula" >> /container/EULAs.txt \
    && cd /tmp \
    && echo "downloading ${NVHPC_URL}" \
    && curl --retry 3 --retry-delay 5 -sSL ${NVHPC_URL} | \
        tar zx \
            --exclude='*/cuda/13.2/*' \
            --exclude='*/math_libs/13.2/*' \
            --exclude='*/comm_libs/*' \
            --exclude='*/profilers/*' \
    && export NVHPC_SILENT="true" \
    && export NVHPC_INSTALL_DIR="/container/nvhpc" \
    && export NVHPC_INSTALL_TYPE="single" \
    && export MARCH_FLAGS=${MARCH_FLAGS} \
    && ./nvhpc_*/install \
    && add_conf "\n# NVHPC version ${NVHPC_VERSION}" \
    && add_conf "export COMPILER_FAMILY=nvhpc" \
    && add_conf "export NVHPC_VERSION=${NVHPC_VERSION}" \
    && add_conf "export NVARCH=$(uname -s)_$(uname -m)" \
    && add_conf "export NVCOMPILERS=${NVHPC_INSTALL_DIR}" \
    && add_conf "export MANPATH=\${MANPATH}:\${NVCOMPILERS}/\${NVARCH}/${NVHPC_VERSION}/compilers/man" \
    && add_conf "export PATH=\${NVCOMPILERS}/\${NVARCH}/${NVHPC_VERSION}/compilers/bin:\$PATH" \
    && add_conf "export MARCH_FLAGS=\"${MARCH_FLAGS}\"" \
    && source /container/config_env.sh \
    && cd ${NVCOMPILERS}/${NVARCH}/${NVHPC_VERSION} \
    && rm_paths="comm_libs/ profilers/ cuda/13.2/ math_libs/13.2/" \
    && echo "Removing extra bloat: ${rm_paths}" > README.whered_stuff_go && (du -hs ${rm_paths} >> README.whered_stuff_go || true) \
    && rm -rf ${rm_paths} \
    && NVHPC_CUDA_VERSION=$(cd ${NVCOMPILERS}/${NVARCH}/${NVHPC_VERSION}/cuda/ && ls * -d | grep -E '[0-9]+\.[0-9]+$') \
    && echo -e "\nNVHPC-Provided CUDA: ${NVHPC_CUDA_VERSION}" \
    && if [[ "${NVHPC_CUDA_VERSION}" == "${CUDA_VERSION}" ]]; then \
           echo "Removing duplicate CUDA (${NVHPC_CUDA_VERSION} == ${CUDA_VERSION})" ;\
           for dir in {cuda,math_libs}/${NVHPC_CUDA_VERSION}; do \
               dir=$(realpath ${dir}) ;\
               parent=$(dirname ${dir}) ;\
               echo "Before: ${parent}" && ls -l ${parent} ;\
               du -hs ${dir} >> README.whered_stuff_go ;\
               rm -rf ${dir} && ln -s ${CUDA_HOME} ${dir} ;\
               echo "After: ${parent}" && ls -l ${parent} ;\
           done ;\
           NVHPC_CUDA_HOME=${CUDA_HOME} ;\
           add_conf "\n# NVHPC-relinked existing CUDA ${NVHPC_CUDA_VERSION}" ;\
           add_conf "export NVHPC_CUDA_VERSION=${NVHPC_CUDA_VERSION}" ;\
           add_conf "export NVHPC_CUDA_HOME=${NVHPC_CUDA_HOME}" ;\
       else \
           NVHPC_CUDA_HOME=$(ls -d ${NVCOMPILERS}/${NVARCH}/${NVHPC_VERSION}/cuda/${NVHPC_CUDA_VERSION}) ;\
           add_conf "\n# NVHPC-embedded CUDA ${NVHPC_CUDA_VERSION}" ;\
           add_conf "export NVHPC_CUDA_VERSION=${NVHPC_CUDA_VERSION}" ;\
           add_conf "export NVHPC_CUDA_HOME=${NVHPC_CUDA_HOME}" ;\
           add_conf "export NVHPC_CUDA_STUBDIR=$(find ${NVHPC_CUDA_HOME} -type d -name stubs)" ;\
           add_conf "export LD_LIBRARY_PATH=/usr/lib64:/usr/lib:\${LD_LIBRARY_PATH}" ;\
           for dir in {cuda,math_libs}/${NVHPC_CUDA_VERSION}; do \
               remove-cuda-static-libs ${dir} >> README.whered_stuff_go ;\
           done ;\
       fi \
    && cat README.whered_stuff_go \
    && find ${NVCOMPILERS} ! -readable -print0 | xargs -0 --no-run-if-empty rm -vf \
    && echo "Configuring ld.so.conf.d..." \
    && echo "${NVHPC_INSTALL_DIR}/$(uname -s)_$(uname -m)/${NVHPC_VERSION}/compilers/lib" >> /etc/ld.so.conf.d/nvhpc.conf \
    && ldconfig --verbose \
    && source /container/config_env.sh \
    && add_conf "export nvhpc_CC=\"$(which nvc)\" && export CC=\"\${nvhpc_CC}\"" \
    && add_conf "export nvhpc_CXX=\"$(which nvc++)\" && export CXX=\"\${nvhpc_CXX}\"" \
    && add_conf "export nvhpc_F77=\"$(which nvfortran)\" && export F77=\"\${nvhpc_F77}\"" \
    && add_conf "export nvhpc_FC=\"$(which nvfortran)\" && export FC=\"\${nvhpc_FC}\"" \
    && add_conf "export CFLAGS=\"-fPIC ${MARCH_FLAGS}\"" \
    && add_conf "export CXXFLAGS=\"-fPIC ${MARCH_FLAGS}\"" \
    && add_conf "export F77FLAGS=\"-fPIC ${MARCH_FLAGS}\"" \
    && add_conf "export FCFLAGS=\"-fPIC ${MARCH_FLAGS}\"" \
    && add_conf "export NVCC_PREPEND_FLAGS=\"-arch=all-major --cudart shared -Wno-deprecated-gpu-targets\"" \
    && docker-clean

LABEL compiler="nvhpc"
LABEL nvhpc="${NVHPC_VERSION}"



#-------------------------------------------------------------------------------
# LLVM (clang) stage - from source
FROM toolkits AS clang-src
#-------------------------------------------------------------------------------
# ref. https://flang.llvm.org/docs/GettingStarted.html
# ref. https://github.com/llvm/llvm-project/blob/main/flang/docs/GettingStarted.md
# ref. https://llvm.org/docs/CMake.html
ARG LLVM_VERSION="21.1.8"
RUN exec &> >(tee /container/logs/llvm.log) \
    && echo "Installing LLVM ${LLVM_VERSION}" \
    && source /etc/os-release \
    && id_tag="|${PLATFORM_ID}|${VERSION_CODENAME}|${ID}|" \
    && case "${id_tag}" in \
        *"almalinux"*|*"|rocky|"*|*"|rhel|"*) \
            ${PKG_INSTALL_CMD} --enablerepo crb \
                ninja-build \
            ;; \
        *"leap|"*|*"tumbleweed|"*) \
            ${PKG_INSTALL_CMD} \
                ninja python311 \
            ;; \
        *"|ubuntu|"*) \
            apt-get update \
            && ${PKG_INSTALL_CMD} \
                ninja-build \
            ;; \
        *) \
            echo "Unrecognized base operating system!!" \
                && echo "id_tag=${id_tag}" \
                && exit 1 \
            ;; \
       esac \
    && LLVM_SRC=/tmp/llvm-project \
    && git clone --branch llvmorg-${LLVM_VERSION} --depth 1 https://github.com/llvm/llvm-project.git ${LLVM_SRC} \
    && df -h && du -hs ${LLVM_SRC} \
    && mkdir -p /tmp/BUILD_ALL && cd /tmp/BUILD_ALL \
    && export LLVM_INSTALL_PATH=/container/llvm/${LLVM_VERSION} \
    && cmake \
           -G Ninja \
           -DCMAKE_BUILD_TYPE=MinSizeRel \
           -DCMAKE_CXX_LINK_FLAGS="-Wl,-rpath,$LD_LIBRARY_PATH" \
           -DCMAKE_CXX_STANDARD=17 \
           -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
           -DCMAKE_INSTALL_PREFIX=${LLVM_INSTALL_PATH} \
           -DFLANG_INCLUDE_TESTS=OFF \
           -DFLANG_RUNTIME_F128_MATH_LIB=libquadmath \
           -DLLVM_BUILD_LLVM_DYLIB=ON \
           -DLLVM_ENABLE_PROJECTS="clang;flang;openmp;lld" \
           -DLLVM_INCLUDE_BENCHMARKS=OFF \
           -DLLVM_INCLUDE_EXAMPLES=OFF \
           -DLLVM_INCLUDE_TESTS=OFF \
           -DLLVM_LINK_LLVM_DYLIB=ON \
           -DLLVM_RAM_PER_LINK_JOB=10000 \
           -DLLVM_TARGETS_TO_BUILD=host \
           ${LLVM_SRC}/llvm \
    && (ninja || ninja || ninja -j 2) \
    && ninja install \
    && add_conf "\n# LLVM clang compilers" \
    && add_conf "export COMPILER_FAMILY=clang" \
    && add_conf "export LLVM_VERSION=${LLVM_VERSION}"  \
    && add_conf "export PATH=${LLVM_INSTALL_PATH}/bin:\${PATH}" \
    && add_conf "export LD_LIBRARY_PATH=${LLVM_INSTALL_PATH}/lib64:${LLVM_INSTALL_PATH}/lib:\${LD_LIBRARY_PATH}" \
    && source /container/config_env.sh \
    && add_conf "export clang_CC=$(which clang) && export CC=\${clang_CC}" \
    && add_conf "export clang_CXX=$(which clang++) && export CXX=\${clang_CXX}" \
    && add_conf "export clang_F77=$(which flang) && export F77=\${clang_F77}" \
    && add_conf "export clang_FC=$(which flang) && export FC=\${clang_FC}" \
    && echo "${LLVM_INSTALL_PATH}/lib" >> /etc/ld.so.conf.d/llvm.conf \
    && ls -d ${LLVM_INSTALL_PATH}/lib/$(uname -m)-* >> /etc/ld.so.conf.d/llvm.conf \
    && ldconfig --verbose \
    && df -h && du -hs /tmp/*/ ${LLVM_INSTALL_PATH} \
    && docker-clean
LABEL compiler="clang"
LABEL clang="${LLVM_VERSION}"



#-------------------------------------------------------------------------------
# LLVM (clang) stage - binary download
FROM toolkits AS clang-binaries
#-------------------------------------------------------------------------------
ARG LLVM_VERSION="21.1.8"
ARG LLVM_URL="https://github.com/llvm/llvm-project/releases/download/llvmorg-21.1.8/LLVM-21.1.8-Linux-ARM64.tar.xz"
RUN exec &> >(tee /container/logs/llvm.log) \
    && echo "Installing LLVM ${LLVM_VERSION}" \
    && echo "downloading ${LLVM_URL}" \
    && export LLVM_INSTALL_PATH=/container/llvm/${LLVM_VERSION} \
    && mkdir -p ${LLVM_INSTALL_PATH} \
    && cd ${LLVM_INSTALL_PATH} \
    && curl --retry 3 --retry-delay 5 -sSL ${LLVM_URL} | tar Jx --strip-components=1 \
    && add_conf "\n# LLVM clang compilers" \
    && add_conf "export COMPILER_FAMILY=clang" \
    && add_conf "export LLVM_VERSION=${LLVM_VERSION}"  \
    && add_conf "export PATH=${LLVM_INSTALL_PATH}/bin:\${PATH}" \
    && add_conf "export LD_LIBRARY_PATH=${LLVM_INSTALL_PATH}/lib64:${LLVM_INSTALL_PATH}/lib:\${LD_LIBRARY_PATH}" \
    && source /container/config_env.sh \
    && add_conf "export clang_CC=$(which clang) && export CC=\${clang_CC}" \
    && add_conf "export clang_CXX=$(which clang++) && export CXX=\${clang_CXX}" \
    && add_conf "export clang_F77=$(which flang) && export F77=\${clang_F77}" \
    && add_conf "export clang_FC=$(which flang) && export FC=\${clang_FC}" \
    && echo "${LLVM_INSTALL_PATH}/lib" >> /etc/ld.so.conf.d/llvm.conf \
    && ls -d ${LLVM_INSTALL_PATH}/lib/$(uname -m)-* >> /etc/ld.so.conf.d/llvm.conf \
    && ldconfig --verbose \
    && df -h && du -hs ${LLVM_INSTALL_PATH} \
    && docker-clean
LABEL compiler="clang"
LABEL clang="${LLVM_VERSION}"



#-------------------------------------------------------------------------------
# LLVM meta stage-select either clang-src or clang-binaries
FROM clang-src AS clang
#-------------------------------------------------------------------------------


#-------------------------------------------------------------------------------
FROM ${COMPILER_FAMILY} AS compilers
RUN echo "Setting ${COMPILER_FAMILY} compiler layer"
#-------------------------------------------------------------------------------



################################################################################
# Part 4: MPIs
################################################################################

#-------------------------------------------------------------------------------
# MPICH variants
#-------------------------------------------------------------------------------

#-------------------------------------------------------------------------------
# MPICH 3.4.x; compatible with Derecho
FROM compilers AS mpich3
# Notes:
#   NVHPC:
#    mpich-3.4.3 does not build with nvhpc's C/C++ compilers, so we will use the system gccs to
#    build mpich, but then set the MPICH_{CC,CXX} vars later for the downstream wrappers.
#    mpich-3.4.3 and nvfortran misunderstand each other regarding real*16, avoid false positive.
#    so we take the rather drastic step of modifying configure.ac to prevent building the f08 interface,
#    no matter what configure *thinks* it finds...
#    does not fPIC by default, so add here.
#   CLANG:
#    ditto f08 interface
#   GCC:
#    modern versions require -fallow-argument-mismatch. Setting FFLAGS and FCFLAGS at configure time
#    will use these flags for both compiling the library and within the resulting wrappers
#    (ref. https://www.mpich.org/static/downloads/3.4.3/mpich-3.4.3-README.txt)
#  -lmpicxx:
#    We can't get mpicxx without -lmpicxx for mpich-v3.x, yet the -lmpicxx bindings are worthless
#    and do not exist on our Cray-EX cray-mpich.  So at minimum, prevent them from being linked,
#    since we would not then be able to override them. - final sed step
#-------------------------------------------------------------------------------
ARG MPICH_VERSION="3.4.3"
RUN exec &> >(tee /container/logs/mpich.log) \
    && echo "building MPICH-${MPICH_VERSION}" \
    && MPICH_INSTALL_PATH="/container/mpich/${MPICH_VERSION}" \
    && cd /tmp && curl --retry 3 --retry-delay 5 -sSL https://www.mpich.org/static/downloads/${MPICH_VERSION}/mpich-${MPICH_VERSION}.tar.gz | tar xz \
    && cd ./mpich-${MPICH_VERSION} \
    && case "${COMPILER_FAMILY}" in \
        "gcc") \
            export FFLAGS="-fallow-argument-mismatch ${FFLAGS}" && export FCFLAGS="-fallow-argument-mismatch ${FCFLAGS}" \
            ;; \
        "nvhpc") \
            sed -i 's/f08_works=yes/f08_works=no/g' configure \
            && export CC="/usr/bin/gcc" \
            && export CXX="/usr/bin/g++" \
            && export CXXFLAGS="-fPIC ${MARCH_FLAGS}" \
            && export CFLAGS="-fPIC ${MARCH_FLAGS}" \
            && export FFLAGS="-fPIC ${MARCH_FLAGS}" \
            && export FCFLAGS="-fPIC ${MARCH_FLAGS}" \
            ;; \
        "clang") \
            sed -i 's/f08_works=yes/f08_works=no/g' configure \
            ;; \
       esac \
    && ./configure --help \
    && (./configure \
           --prefix=${MPICH_INSTALL_PATH} \
           --enable-threads=multiple \
           --with-device=ch4:ofi \
           --enable-silent-rules \
           --disable-static --enable-shared \
           --enable-fortran ${TOGGLE_WITH_CUDA} ${TOGGLE_WITH_ROCM} \
           --disable-dependency-tracking \
            || cat config.log) \
    && make --no-print-directory --jobs ${MAKE_J_PROCS} V=0 \
    && make --no-print-directory --silent install-strip \
    && add_conf "\n# MPICH ${MPICH_VERSION}" \
    && add_conf "export MPI_FAMILY=mpich" \
    && add_conf "export MPI_ROOT=${MPICH_INSTALL_PATH}"  \
    && add_conf "export MPICH_VERSION=${MPICH_VERSION}" \
    && add_conf "export PATH=${MPICH_INSTALL_PATH}/bin:\${PATH}" \
    && echo "${MPICH_INSTALL_PATH}/lib" >> /etc/ld.so.conf.d/mpich-${MPICH_VERSION}.conf \
    && ldconfig --verbose \
    && source /container/config_env.sh \
    && (which mpicxx || { echo "no mpicxx found, build fail?!"; exit 1; } ) \
    && sed -i 's/cxxlibs="-lmpicxx"/cxxlibs= #"-lmpicxx"/g' $(which mpicxx) \
    && case "${COMPILER_FAMILY}" in \
        "nvhpc") \
            echo "# MPICH+nvhpc: built with nvfortran/gcc/g++, but revert to nvhpc C/C++ compilers in the mpicc/mpicxx wrappers" \
            && add_conf "export MPICH_CC=${nvhpc_CC}" \
            && add_conf "export MPICH_CXX=${nvhpc_CXX}" \
            ;; \
       esac \
    && docker-clean
LABEL mpi="mpich"
LABEL mpich="${MPICH_VERSION}"



#-------------------------------------------------------------------------------
# MPICH
FROM compilers AS mpich
# Notes:
#   to restrict yaksa CUDA SM arch, if needed: --with-cuda-sm=turing,ampere,ada,hopper
#-------------------------------------------------------------------------------
ARG MPICH_VERSION="5.0.0"
#ARG MPICH_DEVICE="ch4:ofi,ucx"
ARG MPICH_DEVICE="ch4:ucx"
RUN exec &> >(tee /container/logs/mpich.log) \
    && echo "building MPICH-${MPICH_VERSION}" \
    && env \
    && MPICH_INSTALL_PATH="/container/mpich/${MPICH_VERSION}" \
    && cd /tmp && curl --retry 3 --retry-delay 5 -sSL https://www.mpich.org/static/downloads/${MPICH_VERSION}/mpich-${MPICH_VERSION}.tar.gz | tar xz \
    && cd ./mpich-${MPICH_VERSION} \
    && case "${COMPILER_FAMILY}:${GCC_VERSION}" in \
        "gcc:1[5-9]."*|"gcc:[2-9][0-9]."*) \
            export CFLAGS="-std=gnu17 -Wno-incompatible-pointer-types ${CFLAGS}" \
            ;; \
       esac \
    && ./configure --help \
    && (./configure \
           --prefix=${MPICH_INSTALL_PATH} \
           --enable-threads=multiple \
           --with-device=${MPICH_DEVICE} \
           --enable-silent-rules \
           --disable-static --enable-shared \
           --disable-cxx ${TOGGLE_WITH_CUDA} ${TOGGLE_WITH_ROCM} \
           --disable-dependency-tracking \
            || cat config.log) \
    && make --no-print-directory --jobs ${MAKE_J_PROCS} V=0 \
    && make --no-print-directory --silent install-strip \
    && add_conf "\n# MPICH ${MPICH_VERSION}" \
    && add_conf "export MPI_FAMILY=mpich" \
    && add_conf "export MPI_ROOT=${MPICH_INSTALL_PATH}" \
    && add_conf "export MPICH_VERSION=${MPICH_VERSION}" \
    && add_conf "export PATH=${MPICH_INSTALL_PATH}/bin:\${PATH}" \
    && echo "${MPICH_INSTALL_PATH}/lib" >> /etc/ld.so.conf.d/mpich-${MPICH_VERSION}.conf \
    && ldconfig --verbose \
    && source /container/config_env.sh \
    && (which mpicxx || { echo "no mpicxx found, build fail?!"; exit 1; } ) \
    && docker-clean
LABEL mpi="mpich"
LABEL mpich="${MPICH_VERSION}"



#-------------------------------------------------------------------------------
# OpenMPI variants
#-------------------------------------------------------------------------------

#-------------------------------------------------------------------------------
# OpenMPI
FROM compilers AS openmpi
# Notes:
#   AOCC:
#     problem with half-precision floats; revisit with newer versions (>5.0.8)
#     https://github.com/open-mpi/ompi/issues/13181
#
#   OMPI_ALLOW_RUN_AS_ROOT: distasteful; but unless we want to deal with multiple UIDs in the build/test process we'll allow
#     https://docs.open-mpi.org/en/v5.0.x/man-openmpi/man1/mpirun.1.html#the-allow-run-as-root-option
#-------------------------------------------------------------------------------
ARG OPENMPI_VERSION="5.0.10"
ENV OPENMPI_VERSION="${OPENMPI_VERSION}"
RUN exec &> >(tee /container/logs/openmpi.log) \
    && echo "building OpenMPI-${OPENMPI_VERSION}" \
    && OPENMPI_INSTALL_PATH="/container/openmpi/${OPENMPI_VERSION}" \
    && cd /tmp && curl --retry 3 --retry-delay 5 -sSL https://download.open-mpi.org/release/open-mpi/v$(echo ${OPENMPI_VERSION} | cut -d . -f -2)/openmpi-${OPENMPI_VERSION}.tar.bz2 | tar xj \
    && cd ./openmpi-${OPENMPI_VERSION} \
    && case "${COMPILER_FAMILY}" in \
        "aocc") extra_config_args="--disable-alt-short-float" ;; \
       esac \
    && ./configure --help \
    && (./configure \
           --prefix=${OPENMPI_INSTALL_PATH} ${extra_config_args} ${TOGGLE_WITH_CUDA} ${TOGGLE_WITH_ROCM} \
           --disable-static --enable-shared \
           --disable-dependency-tracking \
            || cat config.log) \
    && make --no-print-directory --jobs ${MAKE_J_PROCS} V=0 \
    && make --no-print-directory --silent install-strip \
    && cd ${OPENMPI_INSTALL_PATH} \
    && rm_paths="share/doc/" \
    && echo "Removing extra bloat: ${rm_paths}" > README.whered_stuff_go && (du -hs ${rm_paths} >> README.whered_stuff_go || true) && cat README.whered_stuff_go \
    && rm -rf ${rm_paths} \
    && add_conf "\n# OpenMPI ${OPENMPI_VERSION}" \
    && add_conf "export MPI_FAMILY=openmpi" \
    && add_conf "export MPI_ROOT=${OPENMPI_INSTALL_PATH}" \
    && add_conf "export OPENMPI_VERSION=${OPENMPI_VERSION}" \
    && add_conf "export PATH=${OPENMPI_INSTALL_PATH}/bin:\${PATH}" \
    && add_conf "export OMPI_ALLOW_RUN_AS_ROOT=1" \
    && add_conf "export OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1" \
    && echo "${OPENMPI_INSTALL_PATH}/lib" >> /etc/ld.so.conf.d/openmpi-${OPENMPI_VERSION}.conf \
    && ldconfig --verbose \
    && source /container/config_env.sh \
    && (which mpicxx || { echo "no mpicxx found, build fail?!"; exit 1; } ) \
    && ompi_info --parsable --all | grep mpi_built_with_cuda_support:value \
    && docker-clean
LABEL mpi="openmpi"
LABEL openmpi="${OPENMPI_VERSION}"



#-------------------------------------------------------------------------------
FROM ${MPI_FAMILY} AS mpi
RUN echo "Setting ${MPI_FAMILY} MPI layer" \
    && add_conf "export CXX=$(which mpicxx)" \
    && add_conf "export CC=$(which mpicc)" \
    && add_conf "export FC=$(which mpifort)" \
    && add_conf "export F77=$(which mpif77)" \
    && add_conf "export MPICXX=$(which mpicxx)" \
    && add_conf "export MPICC=$(which mpicc)" \
    && add_conf "export MPIFC=$(which mpifort)" \
    && add_conf "export MPIF77=$(which mpif77)" \
    && add_conf "TOGGLE_ENABLE_PARALLEL=\"--enable-parallel\"" \
    && docker-clean

ENV CC="mpicc"
ENV CXX="mpicxx"
ENV F77="mpif77"
ENV FC="mpifort"
#-------------------------------------------------------------------------------



################################################################################
# Part 5: Libraries (that may or may not require MPI).
################################################################################

#-------------------------------------------------------------------------------
# HDF5 & NetCDF
FROM ${IOLIBS_PREREQ} AS iolibs
# (ref: https://github.com/Unidata/netcdf-fortran/issues/309 for aocc libtool fix)
# BLOSC / -DCMAKE_POLICY_VERSION_MINIMUM=3.5: required for building with cmake4
#                                             (first encountered on tumbleweed)
#-------------------------------------------------------------------------------
ARG SZIP_VERSION="2.1.1"
ARG BLOSC_VERSION="1.21.6"
#ARG HDF5_VERSION="1.14.6"
#ARG HDF5_URL=https://github.com/HDFGroup/hdf5/releases/download/hdf5_1.14.6/hdf5-1.14.6.tar.gz
ARG HDF5_VERSION="1.12.3"
ARG HDF5_URL=https://github.com/HDFGroup/hdf5/releases/download/hdf5-1_12_3/hdf5-1_12_3.tar.gz
ARG NETCDF_C_VERSION="4.9.3"
ARG NETCDF_FORTRAN_VERSION="4.6.2"
RUN exec &> >(tee /container/logs/iolibs.log) \
    && echo "building IO libraries" \
    && echo "building libszip-${SZIP_VERSION}" \
    && env \
    && SZIP_INSTALL_PATH="/container/szip/${SZIP_VERSION}" \
    && cd /tmp && curl --retry 3 --retry-delay 5 -sSL https://support.hdfgroup.org/ftp/lib-external/szip/${SZIP_VERSION}/src/szip-${SZIP_VERSION}.tar.gz | tar zx \
    && cd ./szip-${SZIP_VERSION} \
    && ./configure --help \
    && (./configure \
            --prefix=${SZIP_INSTALL_PATH} \
            --disable-static --enable-shared \
            --disable-dependency-tracking \
            || cat config.log) \
    && make --no-print-directory --jobs ${MAKE_J_PROCS} V=0 \
    && make --no-print-directory --silent install-strip \
    && echo "#include <stddef.h>" >> ${SZIP_INSTALL_PATH}/include/ricehdf.h \
    && add_conf "\n# SZIP ${SZIP_VERSION}" \
    && add_conf "export SZIP_VERSION=${SZIP_VERSION}" \
    && add_conf "export PATH=${SZIP_INSTALL_PATH}/bin:\${PATH}" \
    && add_conf "export CPATH=${SZIP_INSTALL_PATH}/include:\${CPATH}" \
    && add_conf "export LIBRARY_PATH=${SZIP_INSTALL_PATH}/lib:\${LIBRARY_PATH}" \
    && echo "${SZIP_INSTALL_PATH}/lib" >> /etc/ld.so.conf.d/szip.conf \
    && ldconfig --verbose \
    && source /container/config_env.sh \
    && echo "building libblosc-${BLOSC_VERSION}" \
    && env \
    && BLOSC_INSTALL_PATH="/container/c-blosc/${BLOSC_VERSION}" \
    && cd /tmp && curl --retry 3 --retry-delay 5 -sSL https://github.com/Blosc/c-blosc/archive/v${BLOSC_VERSION}.tar.gz | tar zx \
    && cd ./c-blosc-${BLOSC_VERSION} \
    && cmake \
           -DCMAKE_INSTALL_PREFIX=${BLOSC_INSTALL_PATH} \
           -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
           -DCMAKE_C_STANDARD=11 \
           -DCMAKE_C_STANDARD_REQUIRED=ON \
           -DCMAKE_INSTALL_DO_STRIP=1 \
           -DPREFER_EXTERNAL_ZLIB=ON \
           -DPREFER_EXTERNAL_ZSTD=ON \
           -DPREFER_EXTERNAL_LZ4=ON \
    && make --no-print-directory --jobs ${MAKE_J_PROCS} V=0 \
    && make --no-print-directory --silent install \
    && add_conf "\n# BLOSC ${BLOSC_VERSION}" \
    && add_conf "export BLOSC_VERSION=${BLOSC_VERSION}" \
    && add_conf "export PATH=${BLOSC_INSTALL_PATH}/bin:\${PATH}" \
    && add_conf "export CPATH=${BLOSC_INSTALL_PATH}/include:\${CPATH}" \
    && add_conf "export LIBRARY_PATH=${BLOSC_INSTALL_PATH}/lib64:\${LIBRARY_PATH}" \
    && echo "${BLOSC_INSTALL_PATH}/lib64" >> /etc/ld.so.conf.d/blosc.conf \
    && ldconfig --verbose \
    && source /container/config_env.sh \
    && echo "building HDF5-${HDF5_VERSION}" \
    && env \
    && HDF5_INSTALL_PATH="/container/hdf5/${HDF5_VERSION}" \
    && cd /tmp && curl --retry 3 --retry-delay 5 -sSL ${HDF5_URL} | tar zx \
    && cd ./hdf*/ \
    && case "${COMPILER_FAMILY}" in \
        "nvhpc"|"aocc"|"clang") \
            extra_args="--disable-nonstandard-feature-float16 --disable-tests" \
            ;; \
        *) \
            unset extra_args \
            ;; \
       esac \
    && ./configure --help \
    && (./configure \
            --prefix=${HDF5_INSTALL_PATH} \
            --disable-static --enable-shared \
            --with-default-plugindir \
            --enable-fortran \
            --disable-cxx \
            --enable-hl ${extra_args} ${TOGGLE_ENABLE_PARALLEL} \
            --disable-dependency-tracking \
            || cat config.log) \
    && case "${COMPILER_FAMILY}" in \
        "aocc"|"clang") sed -i -e 's/wl=""/wl="-Wl,"/g' libtool ;; \
       esac \
    && make --no-print-directory --jobs ${MAKE_J_PROCS} V=0 \
    && make --no-print-directory --silent install-strip \
    && make --silent --no-print-directory installcheck \
    && add_conf "\n# HDF5 ${HDF5_VERSION}" \
    && add_conf "export HDF5_VERSION=${HDF5_VERSION}" \
    && add_conf "export PATH=${HDF5_INSTALL_PATH}/bin:\${PATH}" \
    && add_conf "export CPATH=${HDF5_INSTALL_PATH}/include:\${CPATH}" \
    && add_conf "export LIBRARY_PATH=${HDF5_INSTALL_PATH}/lib:\${LIBRARY_PATH}" \
    && echo "${HDF5_INSTALL_PATH}/lib" >> /etc/ld.so.conf.d/hdf5.conf \
    && ldconfig --verbose \
    && source /container/config_env.sh \
    && echo "building NetCDF-${NETCDF_C_VERSION}" \
    && NETCDF_INSTALL_PATH="/container/netcdf" \
    && HDF5_PLUGIN_PATH="${NETCDF_INSTALL_PATH}/lib/plugins" \
    && cd /tmp \
    && curl --retry 3 --retry-delay 5 -sSL https://github.com/Unidata/netcdf-c/archive/v${NETCDF_C_VERSION}.tar.gz | tar zx \
    && curl --retry 3 --retry-delay 5 -sSL https://github.com/Unidata/netcdf-fortran/archive/v${NETCDF_FORTRAN_VERSION}.tar.gz | tar zx \
    && cd /tmp/netcdf-c-${NETCDF_C_VERSION} \
    && ./configure --help \
    && (./configure \
            --prefix=${NETCDF_INSTALL_PATH} \
            --disable-static --enable-shared \
            --enable-plugins \
            --with-plugin-dir=${HDF5_PLUGIN_PATH} \
            --disable-dependency-tracking \
            || cat config.log) \
    && make --no-print-directory --jobs ${MAKE_J_PROCS} V=0 \
    && make --no-print-directory --silent install-strip \
    && make --silent --no-print-directory installcheck \
    && add_conf "\n# NetCDF libraries" \
    && add_conf "export NETCDF=${NETCDF_INSTALL_PATH}" \
    && add_conf "export NETCDF_C_VERSION=${NETCDF_C_VERSION}" \
    && add_conf "export NETCDF_FORTRAN_VERSION=${NETCDF_FORTRAN_VERSION}" \
    && add_conf "export PATH=${NETCDF_INSTALL_PATH}/bin:\${PATH}" \
    && add_conf "export CPATH=${NETCDF_INSTALL_PATH}/include:\${CPATH}" \
    && add_conf "export LIBRARY_PATH=${NETCDF_INSTALL_PATH}/lib:${HDF5_PLUGIN_PATH}:\${LIBRARY_PATH}" \
    && add_conf "export HDF5_PLUGIN_PATH=${HDF5_PLUGIN_PATH}" \
    && echo "${NETCDF_INSTALL_PATH}/lib" >> /etc/ld.so.conf.d/netcdf.conf \
    && echo "${HDF5_PLUGIN_PATH}" >> /etc/ld.so.conf.d/netcdf.conf \
    && ldconfig --verbose \
    && source /container/config_env.sh \
    && cd /tmp/netcdf-fortran-${NETCDF_FORTRAN_VERSION} \
    && ./configure --help \
    && (./configure \
            --prefix=${NETCDF_INSTALL_PATH} \
            CFLAGS="${CFLAGS}" \
            CXXFLAGS="${CXXFLAGS}" \
            FCFLAGS="${FCFLAGS}" \
            --disable-static --enable-shared \
            --enable-zstandard-plugin \
            --disable-dependency-tracking \
            || cat config.log) \
    && case "${COMPILER_FAMILY}" in \
        "aocc"|"clang") sed -i -e 's/wl=""/wl="-Wl,"/g' libtool ;; \
       esac \
    && make --no-print-directory --jobs ${MAKE_J_PROCS} V=0 \
    && make --no-print-directory --silent install-strip \
    && make --silent --no-print-directory installcheck \
    && ldconfig --verbose \
    && (h5pcc -showconfig || h5cc -showconfig) \
    && (nc-config --all && nf-config --all) \
    && docker-clean
LABEL hdf5="${HDF5_VERSION}"
LABEL netcdf-c="${NETCDF_C_VERSION}"
LABEL netcdf-fortran="${NETCDF_FORTRAN_VERSION}"



#-------------------------------------------------------------------------------
# PNetCDF & ParallelIO
FROM iolibs AS mpi-iolibs
#-------------------------------------------------------------------------------
ARG PNETCDF_VERSION="1.14.1"
ARG PIO_VERSION="2.6.6"
ARG PIO_TAG="pio2_6_6"
RUN exec &> >(tee /container/logs/mpi-iolibs.log) \
    && echo "building parallel IO libraries" \
    && echo "building pnetcdf-${PNETCDF_VERSION}" \
    && env \
    && PNETCDF_INSTALL_PATH="/container/pnetcdf/${PNETCDF_VERSION}" \
    && cd /tmp && curl --retry 3 --retry-delay 5 -sSL https://parallel-netcdf.github.io/Release/pnetcdf-${PNETCDF_VERSION}.tar.gz | tar zx \
    && cd ./pnetcdf-${PNETCDF_VERSION} \
    && case "${COMPILER_FAMILY}:${GCC_VERSION}" in \
        "gcc:1[5-9]."*|"gcc:[2-9][0-9]."*|"nvhpc:"*) \
            export CFLAGS="-std=gnu17 ${CFLAGS}" \
            ;; \
       esac \
    && ./configure --help \
    && (./configure \
            --prefix=${PNETCDF_INSTALL_PATH} \
            FCFLAGS="${FCFLAGS}" FFLAGS="${FCFLAGS}" \
            --disable-static --enable-shared \
            --disable-dependency-tracking \
            || cat config.log) \
    && make --no-print-directory --jobs ${MAKE_J_PROCS} V=0 \
    && make --no-print-directory --silent install-strip \
    && add_conf "\n# PNetCDF ${PNETCDF_VERSION}" \
    && add_conf "export PNETCDF=${PNETCDF_INSTALL_PATH}" \
    && add_conf "export PNETCDF_VERSION=${PNETCDF_VERSION}" \
    && add_conf "export PATH=${PNETCDF_INSTALL_PATH}/bin:\${PATH}" \
    && add_conf "export CPATH=${PNETCDF_INSTALL_PATH}/include:\${CPATH}" \
    && add_conf "export LIBRARY_PATH=${PNETCDF_INSTALL_PATH}/lib:\${LIBRARY_PATH}" \
    && echo "${PNETCDF_INSTALL_PATH}/lib" >> /etc/ld.so.conf.d/pnetcdf.conf \
    && ldconfig --verbose \
    && source /container/config_env.sh \
    && echo "building ParallelIO-${PIO_VERSION}" \
    && PIO_INSTALL_PATH="/container/parallelio/${PIO_VERSION}" \
    && cd /tmp && curl --retry 3 --retry-delay 5 -sSL https://github.com/NCAR/ParallelIO/archive/refs/tags/${PIO_TAG}.tar.gz | tar zx \
    && cd ./*${PIO_TAG}/ && mkdir -p build && cd build \
    && cmake \
           -Wno-dev \
           -DCMAKE_BUILD_TYPE=MinSizeRel \
           -DCMAKE_C_STANDARD=11 \
           -DCMAKE_C_STANDARD_REQUIRED=ON \
           -DCMAKE_INSTALL_PREFIX=${PIO_INSTALL_PATH} \
           -DBUILD_SHARED_LIBS=ON \
           -DPIO_ENABLE_FORTRAN=ON \
           -DPIO_ENABLE_NETCDF_INTEGRATION=ON \
           -DWITH_PNETCDF=ON \
           -DNetCDF_C_PATH=${NETCDF} \
           -DNetCDF_Fortran_PATH=${NETCDF} \
           -DPnetCDF_PATH=${PNETCDF} \
           .. \
    && make --no-print-directory --jobs ${MAKE_J_PROCS} V=0 \
    && make --no-print-directory --silent examples \
    && make --no-print-directory --silent install \
    && add_conf "\n# PIO ${PIO_VERSION}" \
    && add_conf "export PIO=${PIO_INSTALL_PATH}" \
    && add_conf "export PIO_VERSION=${PIO_VERSION}" \
    && add_conf "export PATH=${PIO_INSTALL_PATH}/bin:\${PATH}" \
    && add_conf "export CPATH=${PIO_INSTALL_PATH}/include:\${CPATH}" \
    && add_conf "export LIBRARY_PATH=${PIO_INSTALL_PATH}/lib:\${LIBRARY_PATH}" \
    && echo "${PIO_INSTALL_PATH}/lib" >> /etc/ld.so.conf.d/parallelio.conf \
    && ldconfig --verbose \
    && docker-clean
LABEL pnetcdf="${PNETCDF_VERSION}"
LABEL pio="${PIO_VERSION}"



#-------------------------------------------------------------------------------
# FFTW & HEFFTE (with MPI support)
#  (sufficient to build MuRAM)
FROM ${FFTLIBS_PREREQ} AS fftlibs
#-------------------------------------------------------------------------------
ARG FFTW_VERSION="3.3.10"
ARG HEFFTE_VERSION="2.4.1"
RUN exec &> >(tee /container/logs/fftlibs.log) \
    && echo "building fftw-${FFTW_VERSION}" \
    && env \
    && FFTW_INSTALL_PATH="/container/fftw/${FFTW_VERSION}" \
    && cd /tmp && curl --retry 3 --retry-delay 5 -sSL ftp://fftw.org/pub/fftw/fftw-${FFTW_VERSION}.tar.gz | tar zx \
    && cd ./fftw-${FFTW_VERSION} \
    && ./configure --help \
    && (./configure \
            CFLAGS="-O3 ${CFLAGS}" \
            --prefix=${FFTW_INSTALL_PATH} \
            --disable-static --enable-shared --enable-float \
            --enable-mpi --enable-threads \
            --disable-dependency-tracking \
            || cat config.log) \
    && make --no-print-directory --jobs ${MAKE_J_PROCS} V=0 \
    && make --no-print-directory --silent install-strip \
    && make --no-print-directory distclean \
    && (./configure \
            CFLAGS="-O3 ${CFLAGS}" \
            --prefix=${FFTW_INSTALL_PATH} \
            --disable-static --enable-shared \
            --enable-mpi --enable-threads \
            --disable-dependency-tracking \
            || cat config.log) \
    && make --no-print-directory --jobs ${MAKE_J_PROCS} V=0 \
    && make --no-print-directory --silent install-strip \
    && add_conf "\n# FFTW ${FFTW_VERSION}" \
    && add_conf "export FFTW_VERSION=${FFTW_VERSION}" \
    && add_conf "export FFTW_ROOT=${FFTW_INSTALL_PATH}" \
    && add_conf "export PATH=${FFTW_INSTALL_PATH}/bin:\${PATH}" \
    && add_conf "export CPATH=${FFTW_INSTALL_PATH}/include:\${CPATH}" \
    && add_conf "export LIBRARY_PATH=${FFTW_INSTALL_PATH}/lib:\${LIBRARY_PATH}" \
    && echo "${FFTW_INSTALL_PATH}/lib" >> /etc/ld.so.conf.d/fftw.conf \
    && ldconfig --verbose \
    && source /container/config_env.sh \
    && echo "building heffte=${HEFFTE_VERSION} with CPU support" \
    && env \
    && cd /tmp && git clone --branch v${HEFFTE_VERSION} --depth 1 https://github.com/icl-utk-edu/heffte \
    && cd /tmp/heffte && git clean -xdf . && mkdir -p build && cd build \
    && export HEFFTE_INSTALL_PATH="/container/heffte/${HEFFTE_VERSION}-cpu" \
    && cmake \
           -DCMAKE_BUILD_TYPE=Release \
           -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG ${CXXFLAGS}" \
           -DBUILD_SHARED_LIBS=ON \
           -DCMAKE_INSTALL_PREFIX=${HEFFTE_INSTALL_PATH} \
           -DHeffte_ENABLE_FFTW=ON \
           -DFFTW_ROOT=${FFTW_INSTALL_PATH} \
           -DHeffte_ENABLE_CUDA=OFF \
           -DHeffte_ENABLE_ROCM=OFF \
           -DCMAKE_CXX_COMPILER=${CXX} \
           -DMPI_CXX_COMPILER=${CXX} \
           .. \
    && make --no-print-directory --jobs ${MAKE_J_PROCS} V=0 \
    && make --no-print-directory --silent install \
    && case "|${CUDA_HOME}|${ROCM_HOME}|" in \
        "|"*"cuda"*"|") \
            cd /tmp/heffte && git clean -xdf . && mkdir -p build && cd build \
                && echo "building heffte=${HEFFTE_VERSION} with CUDA support" \
                && export HEFFTE_INSTALL_PATH="/container/heffte/${HEFFTE_VERSION}-cuda" \
                && export CPATH="$(realpath $(dirname $(which ${CXX}))/../include):${CPATH}" \
                && cmake \
                       -DCMAKE_BUILD_TYPE=Release \
                       -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG ${CXXFLAGS}" \
                       -DBUILD_SHARED_LIBS=ON \
                       -DCMAKE_INSTALL_PREFIX=${HEFFTE_INSTALL_PATH} \
                       -DHeffte_ENABLE_FFTW=ON \
                       -DFFTW_ROOT=${FFTW_INSTALL_PATH} \
                       -DHeffte_ENABLE_CUDA=ON \
                       -DHeffte_ENABLE_ROCM=OFF \
                       -DCUDAToolkit_ROOT=${CUDA_HOME} \
                       -DCMAKE_CUDA_ARCHITECTURES="80;90" \
                       -DCMAKE_CXX_COMPILER=${CXX} \
                       -DMPI_CXX_COMPILER=${CXX} \
                       .. \
                && make --no-print-directory --jobs ${MAKE_J_PROCS} V=0 \
                && make --no-print-directory --silent install \
          ;; \
       esac \
    && add_conf "\n# HEFFTE ${HEFFTE_VERSION}" \
    && add_conf "export HEFFTE_VERSION=${HEFFTE_VERSION}" \
    && add_conf "export HEFFTE_ROOT=${HEFFTE_INSTALL_PATH}" \
    && add_conf "export PATH=${HEFFTE_INSTALL_PATH}/bin:\${PATH}" \
    && add_conf "export CPATH=${HEFFTE_INSTALL_PATH}/include:\${CPATH}" \
    && add_conf "export LIBRARY_PATH=${HEFFTE_INSTALL_PATH}/lib64:\${LIBRARY_PATH}" \
    && echo "${HEFFTE_INSTALL_PATH}/lib" >> /etc/ld.so.conf.d/heffte.conf \
    && ldconfig --verbose \
    && docker-clean
LABEL fftw="${FFTW_VERSION}"
LABEL heffte="${HEFFTE_VERSION}"



################################################################################
# Final Image
#  notes:
#  remove duplicates from the front of the LD_LIBRARY_PATH
#  handle MANPATH all at once
################################################################################
FROM ${FINAL_TARGET} AS final
COPY extras/ /container/extras/
RUN exec &> >(tee /container/logs/final.log) \
    && list-large-directories \
    && add_conf "\n# Final configuration:" | tee -a /container/config_env.sh \
    && add_conf "export LD_LIBRARY_PATH=$(echo "${LD_LIBRARY_PATH}" | awk -v RS=: -v ORS=: '!seen[$0]++' | sed 's/:$//')" \
    && add_conf "export MANPATH=$(find / -type d -path "*/share/man" -o -path "*/doc/man" | sort -u | tr '\n' ':')" \
    && add_conf "\n# (container image build completed at $(date))" \
    && ( command -v mpicxx >/dev/null 2>&1 \
         && REPORT_PLACEMENT_RUN=0 /container/extras/build_report-placement.sh \
         || echo "no mpicxx in this image; skipping report_placement" ) \
    && useradd plainuser -d /home/plainuser \
    && echo "plainuser 	ALL=(ALL)	NOPASSWD: ALL" >> /etc/sudoers \
    && chown -R plainuser: /container/ \
    && cat /container/config_env.sh

# ref: https://spack.readthedocs.io/en/latest/containers.html
ENTRYPOINT ["/bin/bash", "--rcfile", "/container/config_env.sh", "--login", "-c", "${*}", "--" ]
CMD [ "/bin/bash" ]

# Many github actions do not work as a regular user -- alas, stay root for now.
# && add_conf "export PKG_INSTALL_CMD=\"sudo ${PKG_INSTALL_CMD}\"" \
#USER plainuser
#RUN sudo whoami



#-------------------------------------------------------------------------------
# emacs cruft follows
#-------------------------------------------------------------------------------
# Local Variables:
# mode: sh
# End:
