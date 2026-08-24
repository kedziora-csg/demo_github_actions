# CLAUDE.md

Guidance for working in this repository.

## What this project is

A CI/CD system that builds **multi-layer Docker images of HPC software stacks** and
publishes them to DockerHub (e.g. `ncarcisl/hpcdev-*`, `benjaminkirk/hpcdev-*`). Each image
is a chosen combination of **compiler × MPI × GPU runtime × Linux distro**, with a common
set of scientific libraries (HDF5, NetCDF C/Fortran, PnetCDF, ParallelIO, FFTW, HeFFTe)
built from source on top. Despite the repo name, the substance is the `containers/devenv`
image and the GitHub Actions matrices that drive it.

## Repository layout

- `containers/devenv/Dockerfile` — **the core**, ~1480 lines, 20+ named stages. Almost all
  real work and almost every change happens here.
- `containers/devenv/Makefile` — copies `scripts/` into the build context as `extras/`.
- `containers/{demo,test,publish}/` — small downstream images: demo app, test runner
  (OSU micro-benchmarks etc.), and the final publish/SBOM stage.
- `containers/deploy/ncar-hpc/` — Apptainer/PBS deploy configs for NCAR clusters (Derecho/Casper).
- `scripts/` — `build_*.sh` for real apps (WRF, CESM, ESMF, PETSc, DART, MPAS, Kokkos, …),
  `build_common.cfg` (sourced; sets `INSTALL_ROOT`, `STAGE_DIR`), and `hello_world.*` samples.
- `.github/workflows/` — see CI section below.
- `.github/actions/{slim-action-runner,docker-cleanup}/` — composite actions to free disk
  space on GHA runners and prune Docker between stages.

## Dockerfile architecture (`containers/devenv/Dockerfile`)

A PREREQ-arg-selectable multi-stage DAG. Build-args choose which concrete stage backs each
logical layer:

```
base_os ──▶ [cuda | rocm]? ──▶ miniforge ──▶ toolkits ──▶ <compiler>
   ──▶ <mpi> ──▶ iolibs ──▶ mpi-iolibs ──▶ fftlibs ──▶ final
```

Key selector ARGs (with representative values):
- `BASE_OS` — `almalinux8/9/10`, `rockylinux8/9/10`, `leap`, `tumbleweed`, `jammy`, `noble`
  (+ `*-cuda`/`*-rocm` vendor bases).
- `COMPILER_FAMILY` — `os-gcc` (distro gcc), `gcc` (built from source, `GCC_VERSION`),
  `oneapi`, `aocc`, `nvhpc`, `clang`.
- `MPI_FAMILY` — `openmpi`, `mpich` (5.0.x) / `mpich3` (3.4.x).
- `MINIFORGE_PREREQ`, `TOOLKITS_PREREQ`, `IOLIBS_PREREQ`, `FFTLIBS_PREREQ`, `FINAL_TARGET`
  — wire the DAG (e.g. `MINIFORGE_PREREQ=cuda` layers conda on the CUDA stage; parallel
  HDF5/NetCDF are enabled when `IOLIBS_PREREQ=mpi`).

**Environment accumulation:** stages append `export …` lines to `/container/config_env.sh`
via the `add_conf` helper (PATH, CPATH, LIBRARY_PATH, LD_LIBRARY_PATH, CC/CXX/FC, CFLAGS).
That file is sourced by every login shell and is the single source of truth for the
in-image environment. Each library installs to a versioned prefix under `/container/`.

**nvhpc specifics:** installed from the `cuda_multi` tarball; unused CUDA versions are
dropped at extract time (`tar --exclude='*/cuda/<ver>/*'`) and again via `rm_paths`, and a
CUDA that matches a separately-installed `CUDA_VERSION` is symlinked rather than duplicated
(GHA disk is tight). When bumping NVHPC, update both the `NVHPC_URL` and these `<ver>`
exclude/`rm_paths` strings to the CUDA the new SDK bundles.

## Versions and how to bump them

Defaults live as `ARG`s in the Dockerfile, but the **real, authoritative versions are the
CI matrices**, which override the ARGs via build-args:
- `.github/workflows/matrix-build-images.yaml` — full production matrix.
- `.github/workflows/devel-build-images.yaml` — the PR/dev subset (4 compilers ×
  {almalinux10, noble, leap} × {openmpi, mpich}) plus an apps smoke-test job.

A version bump = edit `compiler_build_args` / `mpi_build_args` / `extra_build_args` in those
two files (library versions like HDF5/NetCDF/PIO live in `extra_build_args`). For NVHPC also
update the CUDA exclude paths in the Dockerfile (above).

**oneAPI URLs come from Spack.** `ONEAPI_CC_URL` and `ONEAPI_FC_URL` are the `cpp` and `ftn`
entries of the `versions` list in Spack's
[`intel_oneapi_compilers/package.py`](https://github.com/spack/spack-packages/blob/develop/repos/spack_repo/builtin/packages/intel_oneapi_compilers/package.py).
Intel embeds an opaque per-release UUID in the path that is **not derivable from the version
number**, so a bump means copying both fresh URLs from there — editing the version inside an
existing URL yields a 404. Both installers are required and must move together
(`intel-dpcpp-cpp-compiler-*` → `icx`/`icpx`, `intel-fortran-compiler-*` → `ifx`); the
oneapi stage guards for all three, because a missing one silently exports an empty
`CC`/`CXX`/`FC` and autoconf/cmake then fall through to the base image's GNU toolchain.
Deliberately **not** the all-in-one toolkit bundle: it lands ~3.8 GB in a SIF against
~1.3 GB for the two components.

**Matrix gotcha:** a new compiler must be added to the base `compiler:` axis list (not just
an `include:` entry). An `include:`-only value that matches no base combination can't merge,
so GitHub creates a single malformed standalone job for it with no `mpi`/`gpu`/`arch` — it
won't fan out across the matrix. (This is why `gcc15`/`gcc16` are in the base list *and*
have `include:` build-args.)

## CI workflows

- `build-hpc-development-image.yaml` — **reusable** build → test → publish; called by the
  others. Builds with `docker/build-push-action`, registry-cache keyed on all build inputs
  (`…-cache` repo), then a separate publish pass via `containers/publish/Dockerfile`.
- `matrix-build-images.yaml` — full matrix (compilers × MPI × GPU × arch), production tags.
- `devel-build-images.yaml` — runs on PRs touching the Dockerfile/scripts; the subset that
  most PRs (including version bumps) are validated against.
- `dial-an-image.yaml` — `workflow_dispatch` to build a single hand-picked variant; the
  fastest way to reproduce/iterate on one failing combination.
- Also: `container-build.yaml`, `conda-build.yaml`, `derived-containers.yaml`,
  `matrix-smoketest-applications.yaml`, `trigger-workflows.yaml`, log-cleanup crons,
  `mega-linter.yml`.

## Conventions & gotchas

- **The system gcc is a load-bearing lever.** A distro's *system* gcc drives three things
  at once: the `os-gcc` compiler family, **nvhpc's host C dialect** (`nvc` inherits the
  system gcc's default `-std`), and the **CUDA runfile installer's gcc version check**. So
  keeping a distro's system gcc at a pre-C23 version (≤14) avoids a whole class of breakage.
  This is why leap is pinned (below) rather than chasing per-library workarounds.
- **gcc-15 / C23 `-std=gnu17` pattern.** GCC ≥15 defaults to `-std=gnu23`; older C code
  (c-blosc, pnetcdf, MPICH, ParallelIO's bundled GPTL) breaks under C23 — typically
  `bool`/`_Bool`/`false`/`true` keyword or `typedef` errors, or "invalid combination of type
  specifiers". Guarded per-library with `case "${COMPILER_FAMILY}:${GCC_VERSION}" in` blocks
  that prepend `-std=gnu17`, matched on `"gcc:1[5-9]."*|"gcc:[2-9][0-9]."*` (i.e. gcc ≥15,
  forward-looking) plus `"nvhpc:"*` as a safety net. These guards are required for the
  explicit **gcc15/gcc16 source-build** compilers on *any* OS, independent of the leap pin.
  Sites: `Dockerfile` ~1002 (MPICH5), ~1291 (pnetcdf). **c-blosc no longer uses a CFLAGS
  arm** — it pins the standard via the CMake cache instead (see the CMake bullet below). If
  a new C library breaks the same way, add the same arm (or the CMake-cache pin if it builds
  with cmake).
- **leap base is pinned.** The `opensuse/leap` rolling tag moved to **Leap 16.0**, which
  ships gcc-15 and dropped the `gcc14` package — that broke nvhpc/leap (C23) and the CUDA
  install. We pin `FROM docker.io/opensuse/leap:15` (`Dockerfile:53`) and `gcc_v=14`
  (`Dockerfile:293`, shared `leap|tumbleweed` arm) to keep a pre-C23 system gcc. If leap's
  system gcc ever must move to ≥15, expect the C23 guards above to start firing for `os-gcc`
  and `nvhpc` on leap too.
- **For CMake builds, pin the C standard via cache, not `$CFLAGS`.** Exporting
  `CFLAGS=-std=gnu17` before `cmake` is unreliable: (a) a bundled sub-project that calls its
  own `project()` ignores it entirely — e.g. PIO's GPTL (`src/gptl/CMakeLists.txt`); and
  (b) even for a flat project like **c-blosc** it is *arch-dependent* — c-blosc's CMake only
  threads the env CFLAGS through on its recognized-CPU (x86 SSE2/AVX2) branch, so on aarch64
  it logs `Unrecognized system processor aarch64` and compiles `shuffle.c` under the
  compiler's default (gcc ≥15 → `-std=gnu23` → `'bool' cannot be defined via typedef`). x86_64
  passed while aarch64 failed for the *same* gcc15/gcc16 build. Fix both with the cache pin:
  `-DCMAKE_C_STANDARD=11 -DCMAKE_C_STANDARD_REQUIRED=ON` (now on **both** the PIO and c-blosc
  cmake lines). `REQUIRED=ON` is essential — without it CMake treats the standard as a minimum
  and won't downgrade from the compiler's newer (C23) default. It's also global, so it applies
  regardless of c-blosc's per-arch CPU branch.
- **gcc-16 + OpenMPI 5.0.10: `always_inline` budget error → combo skipped.** GCC 16 fails the
  build with `inlining failed in call to 'always_inline' 'mca_part_persist_start': --param
  max-inline-insns-single limit reached` (OpenMPI `part_persist.h`). It's a hard `error:` from
  the inliner enforcing the `always_inline` contract — **no `-W`/`-Wno-error` knob demotes it**
  (`-Winline` only covers ordinary `inline`); the only lever is to raise the budget
  (`CFLAGS="--param max-inline-insns-single=20000 …"`). The fix lands upstream in OpenMPI
  5.0.11, so rather than carry the workaround we **exclude `gcc16 + openmpi` in
  `matrix-build-images.yaml`** (see the `reevaluate with 5.0.11` comment) — gcc-15 OpenMPI and
  gcc16+mpich build clean. Drop the exclude once OpenMPI is bumped to ≥5.0.11.
- **nvhpc ignores `-march=`; microarch must be set with `-tp`.** `nvc` does not understand
  GNU's `-march=x86-64-v3`, so passing it via `MARCH_FLAGS` is a silent no-op and nvc falls
  back to its auto-detected default `-tp` = the *build runner's* native CPU (e.g. `-tp znver4`
  → AVX-512). The image then carries instructions the **test/deploy** host may lack → a bare
  `Illegal instruction` (SIGILL) that looks OS-specific but is really build-vs-run runner
  roulette. Use nvc's own ABI-level target instead: `-tp=`, not `-march=`.
  `MARCH_FLAGS` carries whichever spelling the family needs and is declared in the
  **`compilers` stage** so it reaches every family — declared in the nvhpc stage
  alone (as it was) it was accepted and silently ignored for gcc and oneapi, which
  is how four of the six Derecho images ended up baseline `x86-64` with no AVX2.
  The Derecho set now targets its actual CPU: `-march=znver3`, `-tp=zen3`. Leave
  it empty for the general matrices, which must stay portable — and note
  `-march=x86-64-v3` does not merely underperform on the `aarch64` axis, it fails
  to compile. Note `nvc -V` prints the
  *default* `-tp`, not the per-compile one — don't trust it; inspect the binary.
- **`report_cpu_features [binary]`** (`/container/bin/`, a base_os FILE-heredoc) is the tool
  for the above: it prints the host's SIMD level and, given an executable, the ISA it
  *requires* (`readelf -n` note + `objdump` scan for x86 `%zmm` / arm SVE `z*` regs). The
  hello-world smoke tests (`devel`/`matrix-smoketest` workflows, `containers/test`) call it so
  an over-built binary is visible *before* the SIGILL. Reusable for any executable.
- **CUDA 12.9 runfile installer runs its own host-gcc check** ("Failed to verify gcc
  version") that rejects gcc ≥15 — separate from nvcc, and *not* relaxed by the
  `-allow-unsupported-compiler` in `NVCC_PREPEND_FLAGS` (that only affects nvcc compiles).
  Keep cuda-enabled distros on system gcc ≤14, or pass the installer's `--override` flag.
- Other compiler-specific quirks use `case "${COMPILER_FAMILY}" in` (e.g. aocc/clang need
  `--disable-nonstandard-feature-float16` for HDF5 and a libtool `wl=` patch; nvhpc/clang
  disable the MPICH f08 interface; HDF5 forces its own `-std=c99`, so it's C23-immune).
- Bloat control is pervasive: `--disable-static --enable-shared`, `install-strip`,
  `docker-clean`, and removal of docs/profilers/static libs. Keep it lean.
- Spelling/lint via `.cspell.json` and `.mega-linter.yml` (MegaLinter is a separate, often
  noisy check — not part of the image build).

## Useful commands

```bash
gh pr checks <PR#>                       # see which matrix jobs failed
gh run view --job <job-id> --log         # full job log (use --log-failed for just failures)
gh run view --job <job-id> --log > out.log && grep -nE 'error:|Error 2' out.log
gh workflow run dial-an-image.yaml -f …  # rebuild a single variant to reproduce a failure
# pull ONE job's log while the overall run is still in progress (run-level --log refuses):
gh api repos/<owner>/<repo>/actions/jobs/<job-id>/logs > out.log
# authoritative job list (the `--json jobs` view can under-report long matrices):
gh api --paginate "repos/<owner>/<repo>/actions/runs/<run-id>/jobs?per_page=100" -q '.jobs[].name'
```

Notes:
- The real compiler error in a failing image build is usually buried mid-log — the buildx
  "failed to solve" tail only echoes the failing RUN recipe, not the error. Fetch the full
  `--log` and grep for `error:` / `Error 2`.
- `matrix-build-images.yaml` is `workflow_dispatch`-only (one base-OS per dispatch) and runs
  in `max-parallel: 16` waves, so jobs appear in batches — a partial job list early in a run
  is normal, not a dropped matrix.
