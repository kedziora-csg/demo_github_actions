# HPC CI/CD Container Infrastructure

## Overview

This repository defines a continuous integration and continuous delivery (CI/CD) system for building, validating, and publishing containerized high-performance computing (HPC) development environments. The principal deliverables are multi-layer Docker images that combine operating systems, compiler toolchains, MPI implementations, optional GPU runtimes, and commonly used scientific libraries.

The images are intended to provide reproducible development and testing environments for HPC software stacks. Published images use naming patterns such as `ncarcisl/hpcdev-*` and `benjaminkirk/hpcdev-*`.

Although the repository name suggests a GitHub Actions demonstration, the core implementation is the container build system under `containers/devenv` and the GitHub Actions workflows that generate and execute the build matrix.

## Image Composition

Each development image represents a selected combination of several major dimensions:

- Linux distribution
- Compiler family and compiler version
- MPI implementation
- Optional GPU runtime
- Processor architecture
- Scientific library versions

The scientific software stack is built primarily from source. The common library set includes HDF5, NetCDF C, NetCDF Fortran, PnetCDF, ParallelIO, FFTW, and HeFFTe. These libraries are layered on top of the selected compiler and MPI stack so that the final image can be used to build, test, and package HPC applications.

## Repository Layout

The repository is organized around container definitions, build scripts, deployment configuration, and workflow automation.

### Core Development Container

The central file in the repository is:

```text
containers/devenv/Dockerfile
```

This Dockerfile contains the primary build logic. It is a large, multi-stage Dockerfile with more than twenty named stages. Most substantive changes to the container stack occur in this file, including changes to base images, compiler installation, MPI builds, library versions, environment setup, and final image assembly.

The related file:

```text
containers/devenv/Makefile
```

prepares the Docker build context by copying the contents of `scripts/` into the build context as `extras/`. These scripts provide examples of build instructions for HPC simulation software or benchmarking software that rely on the provided development tool chains. 

### Supporting Containers

Several smaller container definitions are located beneath `containers/`:

- `containers/demo/` provides a downstream demonstration image.
- `containers/test/` provides test-runner images, including support for smoke tests and benchmark execution.
- `containers/publish/` provides the final publish and software bill of materials (SBOM) stage.

These containers are downstream consumers of the main development image and support validation, demonstration, or publication workflows.

### HPC Deployment Configuration

The directory:

```text
containers/deploy/ncar-hpc/
```

contains deployment configuration for NCAR HPC systems, including Apptainer and PBS-related assets for systems such as Derecho and Casper.

[This section needs to be expanded.]

### Build and Application Scripts

The `scripts/` directory contains build helpers for larger HPC applications and software frameworks, including examples such as WRF, CESM, ESMF, PETSc, DART, MPAS, and Kokkos. It also contains sample programs such as `hello_world.*`.

The shared file:

```text
scripts/build_common.cfg
```

is sourced by build scripts and defines common variables such as `INSTALL_ROOT` and `STAGE_DIR`.

### GitHub Actions Configuration

The `.github/workflows/` directory contains the automation that drives image construction, testing, and publication. Important workflows include:

- `build-hpc-development-image.yaml`
- `matrix-build-images.yaml`
- `devel-build-images.yaml`
- `dial-an-image.yaml`
- `matrix-smoketest-applications.yaml`

The `.github/actions/` directory also contains composite actions used by the workflows. In particular, `slim-action-runner` and `docker-cleanup` help manage limited disk space on GitHub-hosted runners.

## Dockerfile Architecture

The primary development image is built from a parameterized multi-stage Dockerfile. The Dockerfile is organized as a directed acyclic graph of build stages, where Docker build arguments select which concrete stage should provide each logical layer.

At a high level, the build graph follows this structure:

```text
base_os -> optional GPU runtime -> miniforge -> toolkits -> 
compiler -> MPI -> I/O libraries -> MPI I/O libraries -> 
FFT libraries -> final image
```

The major logical layers are:

1. Base operating system
2. Optional CUDA or ROCm runtime
3. Miniforge Python environment
4. General development toolkits
5. Compiler toolchain
6. MPI implementation
7. Serial scientific I/O libraries
8. MPI-enabled I/O libraries
9. FFT and numerical libraries
10. Final image assembly

This design allows a single Dockerfile to produce many variants without duplicating large sections of build logic.

## Build Argument Selection

Docker build arguments determine how the multi-stage graph is assembled. Representative selector arguments include:

- `BASE_OS`
- `COMPILER_FAMILY`
- `MPI_FAMILY`
- `MINIFORGE_PREREQ`
- `TOOLKITS_PREREQ`
- `IOLIBS_PREREQ`
- `FFTLIBS_PREREQ`
- `FINAL_TARGET`

The `BASE_OS` argument selects the Linux distribution or vendor-provided base image. Representative values include AlmaLinux, Rocky Linux, openSUSE Leap, openSUSE Tumbleweed, Ubuntu Jammy, and Ubuntu Noble variants, with additional CUDA or ROCm variants where applicable.

## Base OS Comparison Summary

Choosing the right base image for your CI/CD pipelines dictates your build speed, security posture, and how closely your testing environment mirrors production. The six distributions you mentioned fall into three distinct families, each optimizing for a different point on the stability-versus-freshness spectrum.

## The Enterprise Linux Derivatives

When your production environment runs Red Hat Enterprise Linux (RHEL), using a RHEL-compatible distribution for your containers ensures your CI/CD pipeline tests exactly what will be deployed.

### AlmaLinux

AlmaLinux is a community-owned enterprise operating system governed by a non-profit foundation. Following Red Hat's source code availability changes in 2023, AlmaLinux shifted its focus to maintaining **Application Binary Interface (ABI) compatibility** with RHEL, pulling sources from CentOS Stream and RHEL to ensure software runs identically.

* **Advantages:** Extreme stability and a 10-year support lifecycle. It is highly reliable for mirroring enterprise production environments without incurring licensing costs.
* **Disadvantages:** It prioritizes stability over freshness. You will not have access to the latest compilers or language runtimes out of the box without enabling modular or third-party repositories. Base images also tend to be larger than minimal Alpine or Debian images.

### Rocky Linux

Rocky Linux is another free enterprise operating system, founded by the original creator of CentOS. Unlike AlmaLinux, Rocky Linux continues to target **1:1 bug-for-bug compatibility** with RHEL.

* **Advantages:** It shares the exact same technical benefits as AlmaLinux — enterprise-grade stability, long lifecycles, and a behavior profile identical to RHEL.
* **Disadvantages:** The technical drawbacks are identical to AlmaLinux (older toolchains, larger footprint). The primary difference is organizational: Rocky Linux is backed by a B-Corp (the Rocky Enterprise Software Foundation) rather than a non-profit foundation.

## The SUSE Ecosystem

The openSUSE family is deeply integrated into European enterprise environments and offers robust package management through Zypper.

### openSUSE Leap

Leap is a regular point-release distribution built from the exact same binary packages as SUSE Linux Enterprise (SLE).

* **Advantages:** Excellent predictability and enterprise-grade stability. It strikes a balance between stability and updated software, making it highly reliable for pipelines deploying to SUSE-heavy infrastructure.
* **Disadvantages:** The openSUSE container ecosystem is smaller than those of RHEL and Ubuntu. You may find fewer pre-built third-party Docker layers and community troubleshooting guides.

### openSUSE Tumbleweed

Tumbleweed is a rolling-release distribution, meaning it continuously updates packages to the latest stable versions rather than relying on massive point-release upgrades.

* **Advantages:** It provides immediate access to the absolute newest kernels, compilers, and language versions. It is highly valuable in CI/CD when you need to test your application against upcoming software updates to catch breaking changes early.
* **Disadvantages:** A rolling release is a double-edged sword in automation. If you do not strictly pin your dependencies, an upstream package update can unexpectedly break a previously functioning CI/CD pipeline.

## The Ubuntu LTS Series

Ubuntu Long Term Support (LTS) releases are the default standard for many cloud-native applications due to their massive community and broad compatibility.

### Ubuntu Jammy (22.04 LTS)

Released in 2022, Jammy Jellyfish is an established, widely adopted LTS release.

* **Advantages:** Universal compatibility. Almost every third-party vendor builds and tests for Ubuntu 22.04 first. The ecosystem is massive, making troubleshooting and finding optimized base layers effortless.
* **Disadvantages:** Because the core packages are locked to their 2022 versions, developers relying on newer language features (like recent Python or GCC updates) often have to build them from source or pull from external repositories.

### Ubuntu Noble (24.04 LTS)

Released in 2024, Noble Numbat is Canonical's current LTS standard.

* **Advantages:** It updates the entire software stack to modern standards, providing newer default toolchains, security features, and language runtimes out of the box, while still guaranteeing 5 years of standard support.
* **Disadvantages:** Being newer, some legacy proprietary software or older CI/CD build scripts may still expect the Jammy environment and might require slight modifications to run smoothly.

---

## Base Image Comparison Summary

| Distribution | Release Model | CI/CD Sweet Spot |
| --- | --- | --- |
| **AlmaLinux** | Point Release (10-yr LTS) | Mirroring RHEL production environments. |
| **Rocky Linux** | Point Release (10-yr LTS) | Mirroring RHEL production environments. |
| **openSUSE Leap** | Point Release (Aligned with SLE) | Stable pipelines targeting SUSE infrastructure. |
| **openSUSE Tumbleweed** | Rolling Release | Testing applications against bleeding-edge dependencies. |
| **Ubuntu Jammy** | Point Release (2022 LTS) | Maximizing compatibility with legacy tools and scripts. |
| **Ubuntu Noble** | Point Release (2024 LTS) | Modern cloud-native development with long-term stability. |



The `COMPILER_FAMILY` argument selects the compiler layer. Supported compiler families include distribution GCC, GCC built from source, Intel oneAPI, AOCC, NVHPC, and Clang.

The `MPI_FAMILY` argument selects the MPI implementation, typically OpenMPI, MPICH 5.x, or MPICH 3.4.x.

The `*_PREREQ` arguments are especially important because they connect the logical stages in the Docker build graph. For example, setting `MINIFORGE_PREREQ=cuda` places the Miniforge layer on top of the CUDA-enabled stage. Similarly, setting `IOLIBS_PREREQ=mpi` enables parallel HDF5 and NetCDF builds by placing the I/O library stage after the MPI stage.

## Environment Configuration

The in-container environment is accumulated through the file:

```text
/container/config_env.sh
```

Individual Docker stages append `export` statements to this file through the `add_conf` helper. The file contains paths, compiler settings, and library search configuration, including values such as:

- `PATH`
- `CPATH`
- `LIBRARY_PATH`
- `LD_LIBRARY_PATH`
- `CC`
- `CXX`
- `FC`
- `CFLAGS`

This configuration file is sourced by login shells inside the container and serves as the authoritative description of the runtime environment. Because the image is assembled from many independently built layers, a centralized environment file helps ensure that later stages can locate compilers, headers, libraries, and tools installed by earlier stages.

Libraries are installed under versioned prefixes beneath `/container/`, which makes the image layout explicit and easier to inspect. This is similar to the often used /opt/ directory for optional software for a given operating system implementation. For example in the `hpcdev-x86_64:noble-gcc15-mpich-26.07` container, 

```
# ls container/
# ls container/       
bin	       extras  hdf5	      logs	 netcdf		       pnetcdf
c-blosc        fftw    heffte	      miniforge  osu-micro-benchmarks  szip
config_env.sh  gcc     init-conda.sh  mpich	 parallelio
```

This is provides the added elements for the HPC development tool chain. Examples of applications that can be built based on this tool chain are in the subdirectory `examples`.

```
# ls container/extras/
Dockerfile	           build_mpas.sh		          hello_world.f90
build_cesm.sh	       build_muram.sh		          hello_world_mpi.cxx
build_common.cfg       build_osu-micro-benchmarks.sh  install_conda.sh
build_cuda_samples.sh  build_petsc.sh		          install_npl.sh
build_dart.sh	       build_wrf.sh		              parallel_stl_sort.cxx
build_esmf.sh	       hello_world.c		          remove_static_bloat.sh
build_fasteddy.sh      hello_world.cu		          update_headers.pl
build_kokkos.sh        hello_world.cxx
```

## NVHPC-Specific Handling

NVHPC is installed from the `cuda_multi` tarball. Because GitHub Actions runners have limited disk capacity, unused CUDA versions bundled inside the NVHPC SDK are excluded during extraction and removed again after installation.

When the separately installed CUDA version matches the CUDA version bundled with NVHPC, the build avoids duplicating the CUDA installation by using a symlink.

When updating NVHPC, maintainers must update both the `NVHPC_URL` and the CUDA version-specific exclusion and removal paths. These paths must correspond to the CUDA version bundled by the new NVHPC SDK.

## Version Management

Default software versions are declared as Dockerfile `ARG` values. However, the authoritative versions used in CI are defined by the GitHub Actions matrices, which override the Dockerfile defaults through build arguments.

The most important workflow files for version management are:

```text
.github/workflows/matrix-build-images.yaml
.github/workflows/devel-build-images.yaml
```

A typical version bump requires editing one or more of the following matrix entries:

- `compiler_build_args`
- `mpi_build_args`
- `extra_build_args`

Library versions such as HDF5, NetCDF, ParallelIO, and related dependencies are generally controlled through `extra_build_args`.

For NVHPC updates, the Dockerfile must also be reviewed for CUDA exclusion and removal paths, because these paths are tied to the CUDA version bundled with the NVHPC SDK.

## GitHub Actions Workflows

### Reusable Build Workflow

The workflow:

```text
.github/workflows/build-hpc-development-image.yaml
```
- Purpose: Reusable workflow that builds, tests, and **optionally publishes** a single HPC development image variant.
- Triggers: `workflow_call` from other workflows.
- Key jobs:
  - `build-image`: Build image layers, run image tests, and publish deployment image/tags when enabled.

It is the main workflow for building the HPC containers. It is invoked by other workflows and performs the core Docker image build using `docker/build-push-action`.

This workflow uses registry-backed cache repositories keyed on build inputs. After the build and test phases, a separate publication pass is performed through `containers/publish/Dockerfile`.

The workflow named, "BaseOS + Toolkits + Compiler + MPI Image" does the actual building of the Docker image. `tags: ${{ env.CI_TAG }}` provides that name based on 

```
CI_REPO=${PUBLISH_REPO}-cache
CI_TAG=${CI_REPO}:${{ inputs.os }}${COMPILER_LABEL}${MPI_LABEL}${GPU_LABEL}-${INPUTS_SHA}
```

It produces registry name something like:
```
ncarcisl/hpcdev-x86_64-cache:almalinux9-gcc-openmpi-a1b2c3d
```

This is not the same as the public facing tag for the published image, which is set in "Publish Image" workflow. This uses 

```
PUBLISH_REPO=${{ inputs.docker_repo }}-${{ inputs.arch }}
          PUBLISH_TAG=${PUBLISH_REPO}:${{ inputs.os }}${COMPILER_LABEL}${MPI_LABEL}${GPU_LABEL}-latest
```

and looks like 

```
ncarcisl/hpcdev-x86_64:almalinux9-gcc-openmpi-latest
```

The cached image can be used as a layer for the next image for BuildKit to use to add layers, while the published one appears in the Docker Hub for users to pull. That matters for this repository because the Dockerfile builds expensive HPC libraries from source. Without a cache, small changes could force long rebuilds of compilers, MPI, HDF5, NetCDF, FFT libraries, and related toolchain dependencies. The layering is input and output is defined in `cache-from:` and `cache-to:` in the "BaseOS + Toolkits + Compiler + MPI Image" workflow. 

The build-args: block in the workflow becomes Docker/BuildKit --build-arg KEY=VALUE inputs. In the Dockerfile they show up as ARG declarations, for example at the top of the Dockerfile. For a typical almalinux9 + gcc + openmpi + nogpu build, this might effectively become:

```
BASE_OS=almalinux9
FINAL_TARGET=fftlibs
MINIFORGE_PREREQ=base_os
HDF5_VERSION=1.14.5
NETCDF_C_VERSION=4.10.0
NETCDF_FORTRAN_VERSION=4.6.3
PNETCDF_VERSION=1.14.1
PIO_VERSION=2.6.7
PIO_TAG=pio2_6_7
FFTW_VERSION=3.3.10
HEFFTE_VERSION=2.4.1
COMPILER_FAMILY=os-gcc
MPI_FAMILY=openmpi
OPENMPI_VERSION=5.0.10
```

### Full Production Matrix

The workflow:

```text
.github/workflows/matrix-build-images.yaml
```

- Purpose: Main production matrix build workflow across compilers, MPI stacks, GPU options, and architectures.
- Triggers: `workflow_dispatch`.
- Key jobs:
  - `build-matrix`: Calls the reusable build workflow for full matrix combinations.

It defines the full production matrix. It expands across compiler, MPI, GPU, architecture, and operating-system combinations. 

matrix workflow
  -> reusable workflow inputs
  -> docker/build-push-action build-args
  -> Dockerfile ARG declarations
  -> selected stages, package versions, build behavior
  -> selected values optionally exported into /container/config_env.sh

### Development Matrix

The workflow:

```text
.github/workflows/devel-build-images.yaml
```

- Purpose: Development/PR matrix build of selected compiler/OS/MPI combinations, followed by in-container app smoke tests.
- Triggers: `workflow_dispatch`, `pull_request` (Dockerfile and scripts paths).
- Key jobs:
  - `build-matrix`: Calls the reusable build workflow for a reduced validation matrix.
  - `apps-matrix`: Waits on `build-matrix`, then compiles/tests apps inside built images.

Runs on pull requests that touch the Dockerfile, scripts, or related workflow files. It uses a reduced matrix intended to validate common and important combinations without incurring the full cost of the production matrix.

This workflow is the primary validation path for routine development and version-bump pull requests.

### Dial-an-Image Workflow

The workflow:

```text
.github/workflows/dial-an-image.yaml
```

- Purpose: On-demand single-variant builder to reproduce and iterate quickly on one configuration.
- Triggers: `workflow_dispatch`.
- Key jobs:
  - `build-image`: Calls the reusable build workflow using user-selected inputs.

It is the preferred tool for reproducing and debugging one failing combination without launching the full matrix.

### trigger-workflows.yaml

- Purpose: Orchestrator workflow that dispatches matrix builds for selected base operating systems.
- Triggers: `workflow_dispatch`, monthly `schedule`, and closed `pull_request` on `main`.
- Key jobs:
  - `trigger-workflows`: Uses GitHub CLI to dispatch `matrix-build-images.yaml` multiple times.

### container-build.yaml

- Purpose: Validates software builds inside pre-existing container images (hello world, DART, optional Kokkos).
- Triggers: `workflow_dispatch`, `push` to `ci_cd`.
- Key jobs:
  - `build-from-container-env`: Runs compile/test tasks in matrix-selected container runtimes.

### Additional Workflows

Other workflows include `conda-build.yaml`, `derived-containers.yaml`, `matrix-smoketest-applications.yaml`, log-cleanup schedules, and `mega-linter.yml`.

## Matrix Design Considerations

When adding a new compiler that should participate in the full matrix, the compiler must be added to the base `compiler:` axis list. It is not sufficient to add the value only through an `include:` entry.

If an `include:` entry does not match an existing base matrix combination, GitHub Actions creates a standalone job for that included value. Such a job will not automatically fan out across the MPI, GPU, architecture, and operating-system dimensions. The result can be a malformed job that lacks expected matrix fields such as `mpi`, `gpu`, or `arch`.

For this reason, compiler variants such as `gcc15` and `gcc16` must appear in the base compiler axis when they are expected to combine with the rest of the matrix.

## Compiler and Language Standard Issues

### System GCC as a Load-Bearing Dependency

The system GCC version supplied by a base distribution affects multiple parts of the image build. It controls the `os-gcc` compiler family, influences NVHPC host C dialect behavior, and affects the CUDA runfile installer's GCC compatibility check.

Keeping the system GCC version at a pre-C23 default, generally GCC 14 or earlier, avoids several classes of failures in older C libraries, NVHPC builds, and CUDA installation.

### GCC 15 and Later

GCC 15 and later default to a C23 dialect. Several older C codebases in the stack are not fully compatible with that default. Failures commonly appear as errors involving `bool`, `_Bool`, `false`, `true`, typedef conflicts, or invalid combinations of type specifiers.

Affected libraries have guarded build logic that pins the C standard back to a compatible dialect. For Autotools-based builds, this is commonly handled by adding `-std=gnu17` to `CFLAGS` for GCC 15 and newer, with additional handling for NVHPC when appropriate.

For CMake builds, the preferred approach is to set the C standard through the CMake cache:

```text
-DCMAKE_C_STANDARD=11 -DCMAKE_C_STANDARD_REQUIRED=ON
```

This approach is more reliable than exporting `CFLAGS` before running CMake. Bundled subprojects may call their own `project()` command and ignore inherited environment flags. Architecture-specific branches in upstream CMake logic may also fail to propagate environment flags consistently.

The `CMAKE_C_STANDARD_REQUIRED=ON` setting is important because it instructs CMake to enforce the requested standard rather than treating it as a minimum.

### OpenMPI and GCC 16

GCC 16 exposes an OpenMPI 5.0.10 build failure involving an `always_inline` function and the compiler's inline instruction budget. This is a hard compiler error, not a warning that can be demoted with a `-Wno-error` option.

The practical workaround is to raise the compiler's inline budget, but the expected upstream fix is in OpenMPI 5.0.11. Until that version is used, the `gcc16 + openmpi` combination is excluded from the production matrix.

### NVHPC Microarchitecture Flags

NVHPC does not interpret GNU-style `-march=` flags in the same way as GCC. Passing a value such as `-march=x86-64-v3` to `nvc` can be ineffective, causing the compiler to fall back to the build runner's native target.

This can produce binaries that require instructions not available on the eventual test or deployment host. The resulting runtime failure often appears as `Illegal instruction`.

For NVHPC, the matrix should use NVHPC's target-processor syntax:

```text
-tp=x86-64-v3
```

instead of GNU-style `-march=` syntax.

### CUDA Installer GCC Checks

The CUDA runfile installer performs its own host-GCC compatibility check. This check is separate from `nvcc` compilation and is not relaxed by `NVCC_PREPEND_FLAGS` or `-allow-unsupported-compiler`.

CUDA-enabled distributions should therefore use a system GCC version accepted by the installer, or the installer must be invoked with an explicit override when appropriate.

## Operating-System Constraints

Base operating systems affect package availability, compiler versions, CUDA compatibility, and vendor compiler behavior.

The openSUSE Leap base is pinned to Leap 15 because the rolling `opensuse/leap` tag moved to Leap 16.0, which ships GCC 15 and removed the `gcc14` package. That change introduced C23-related issues for NVHPC and caused CUDA installer failures. Pinning Leap to version 15 keeps the system compiler in the expected pre-C23 range.

If Leap or another base distribution must move to GCC 15 or newer, maintainers should expect the existing C23 compatibility guards to become relevant for additional compiler and operating-system combinations.

## Library Build Strategy

The scientific libraries are generally built from source and installed under `/container/` using versioned prefixes. The build strategy emphasizes:

- Shared libraries rather than static libraries
- `install-strip` where supported
- Removal of documentation, profilers, static archives, and temporary files where possible
- Consistent compiler and MPI wrapper usage
- Reproducible version selection through workflow matrix build arguments

This approach preserves the functionality needed for HPC development while controlling image size.

## Image Size Control

Image size is an ongoing concern because CI runners have limited disk capacity and HPC software stacks can be large. The Dockerfile therefore uses several size-control practices, including:

- `--disable-static`
- `--enable-shared`
- `install-strip`
- Explicit cleanup commands
- Docker cleanup steps between workflow phases
- Removal of unused CUDA content from NVHPC installations
- Removal of documentation and build-only artifacts

Maintainers should preserve these practices when modifying build logic.

## CPU Feature Diagnostics

The container includes the diagnostic utility:

```text
/container/bin/report_cpu_features
```

This tool reports the host's SIMD capability. When given an executable, it also attempts to identify the instruction set required by that binary by inspecting ELF notes and scanning disassembly output.

This tool is particularly useful for diagnosing `Illegal instruction` failures. Such failures often indicate that a binary was built for a newer CPU target than the runtime host supports.

The hello-world smoke tests in the development and matrix-smoketest workflows call this utility so that over-targeted binaries are visible before they fail at runtime.

## Debugging CI Failures

The actual compiler error in a failing image build is often located far above the final BuildKit failure message. The final `failed to solve` output usually identifies the failing Docker `RUN` instruction, but it may not include the underlying compiler or linker error.

Useful commands include:

```bash
gh pr checks <PR_NUMBER>
gh run view --job <JOB_ID> --log
gh run view --job <JOB_ID> --log-failed
gh run view --job <JOB_ID> --log > out.log
grep -nE 'error:|Error 2' out.log
```

When a workflow run is still in progress and the run-level log is unavailable, a single job log can be retrieved through the GitHub API:

```bash
gh api repos/<OWNER>/<REPO>/actions/jobs/<JOB_ID>/logs > out.log
```

For large matrix runs, the GitHub CLI's `--json jobs` output can under-report jobs while the run is still expanding. The more authoritative approach is:

```bash
gh api --paginate \
  "repos/<OWNER>/<REPO>/actions/runs/<RUN_ID>/jobs?per_page=100" \
  -q '.jobs[].name'
```

Because `matrix-build-images.yaml` is manually dispatched one base operating system at a time and uses `max-parallel: 16`, jobs appear in waves. A partial job list early in the run is normal and does not necessarily indicate that matrix entries were dropped.

## Maintenance Guidelines

When maintaining this repository, follow these guidelines:

1. Treat `containers/devenv/Dockerfile` as the primary implementation file for the HPC development image.
2. Treat the GitHub Actions matrices as the authoritative source for CI build versions.
3. Validate routine changes through `devel-build-images.yaml` before running the full production matrix.
4. Use `dial-an-image.yaml` to reproduce a single failing variant.
5. Add new matrix values to the base matrix axes when full fan-out is required.
6. Be cautious when changing system compiler versions, because they affect CUDA, NVHPC, and `os-gcc` behavior simultaneously.
7. Prefer explicit compiler and language-standard settings over relying on compiler defaults.
8. For CMake projects, prefer CMake cache variables over environment `CFLAGS` when pinning the C standard.
9. Preserve image-size controls such as shared-only builds, stripped installs, and cleanup steps.
10. Inspect full CI logs to find the first real compiler or linker error rather than relying only on the final BuildKit summary.

## Summary

This repository implements a matrix-driven CI/CD system for HPC development containers. Its design relies on a parameterized multi-stage Dockerfile, centralized environment configuration, source-built scientific libraries, and GitHub Actions workflows that expand and validate many compiler, MPI, GPU, architecture, and operating-system combinations.

The most important maintenance concerns are matrix correctness, compiler compatibility, image size control, and efficient diagnosis of long-running container build failures.
