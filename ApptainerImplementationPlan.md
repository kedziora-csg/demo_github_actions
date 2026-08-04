# `report_placement` — a hybrid MPI+OpenMP placement diagnostic

## Context

The starting point was `scripts/hello_world_mpi.cxx`: one line per MPI rank with
a *thread count*, and an OpenMP region that ran before `MPI_Init`. That could not
answer the question this repository actually needs answered on Derecho:

> When a containerized hybrid job is launched through PBS/PALS and Apptainer,
> does each rank and thread end up where we think it does, and is the container
> really using Cray MPICH over the CXI fabric?

The result is `report_placement` — a companion to the existing
`/container/bin/report_cpu_features`. That one reports what the host CPU *is*;
this one reports where each rank and thread actually *landed* on it. It is baked
into every image at `/container/bin/report_placement` and exercised in CI by
`build-hpc-dev-image-ghcr.yaml` (driven by `dial-an-image-ghcr.yaml`).

A trivial `scripts/hello_world_mpi.cxx` was restored under its original name, so
the repository keeps a genuine "simplest possible MPI+OpenMP program" for
teaching. `containers/demo` still builds `speak.exe` from it.

**Status: implemented and verified.** See *Verification* at the end for what has
and has not been exercised.

---

## 1. `scripts/report_placement.cxx`

### Constraints that shaped it

Five call sites compile this file with exactly `mpicxx -o <exe> <src> -fopenmp`
— no `-std=`, no extra libraries, no extra include paths:

| Call site | Note |
|---|---|
| `containers/test/Dockerfile:44` | `&&`-chained → a non-zero exit breaks the image build |
| `.github/workflows/devel-build-images.yaml:161` | `&&`-chained, no `\|\| true` |
| `.github/workflows/matrix-smoketest-applications.yaml:99` | `&&`-chained, no `\|\| true` |
| `.github/workflows/container-build.yaml:94` | via the `src/` symlink; has `\|\| true` |
| `containers/devenv/Dockerfile` (`final` stage) | via `build_report-placement.sh` |

Consequences, all load-bearing:

1. **It always exits 0.** Degraded conditions (no sysfs, oversubscription, a low
   `provided` thread level) are reported in-band, never fatal. Two ranks × two
   threads on a 2-core CI runner is guaranteed oversubscribed and must not fail
   a build.
2. **No hwloc, no libnuma, no `-lpthread`.** Everything comes from
   `sched.h` / `syscall` / sysfs, which need only `_GNU_SOURCE`.
3. **Written to C++11, avoiding C++17-only spellings**, because no `-std=` is
   passed and the language level is whatever each compiler defaults to across
   the base-OS matrix (`nvc++` and `icpx` default to C++17; gcc varies by distro).

### How it works

- `#define _GNU_SOURCE` is the first non-comment line, guarded by `#ifndef`.
  glibc latches feature-test macros in `<features.h>`, which the first system
  header pulls in — an include hoisted above it silently removes `sched_getcpu`
  and `sched_getaffinity`.
- `MPI_Init_thread(..., MPI_THREAD_FUNNELED, &provided)` is the first statement
  in `main`, and the OpenMP region comes strictly after it. A thread pool created
  before MPI init binds against the *pre*-init affinity, hiding whatever the PMI
  bootstrap, a GTL `LD_PRELOAD`, or GPU init did to the process. `FUNNELED` is
  the honest contract and avoids Cray MPICH's slower thread-safe path.
- **CPU and NUMA node** come from `syscall(SYS_getcpu, ...)` — no glibc-version
  dependency, and it returns the NUMA node for free. `sched_getcpu()` is the
  fallback, behind a *nested* `__GLIBC_PREREQ` test.
- **Core / socket / L3** come from sysfs, resolved *after* the parallel region
  for exactly the CPUs observed. `thread_siblings_list`'s first element is a
  globally unique physical core id (`core_id` alone is only unique within a
  package). The L3 group is found by scanning `cache/index*/level` for level 3 —
  the index numbering is not guaranteed, so `index3` must not be assumed.
- **Affinity mask** comes from `sched_getaffinity(0, ...)` inside the region;
  pid 0 means the calling *task*, and on Linux threads are tasks, so this yields
  the per-thread mask rather than the process mask.
- **It configures nothing** — no `omp_set_num_threads`, no `proc_bind` clause,
  no `setenv`. Its entire value is passively observing what `OMP_NUM_THREADS`,
  `OMP_PROC_BIND`, `OMP_PLACES`, PBS `ompthreads` and PALS `--cpu-bind` produced.

**A subtlety worth preserving:** topology must be resolved from the CPUs the
threads were *observed* on, not from the process affinity mask sampled up front.
OpenMP binds threads with `sched_setaffinity`, which is bounded by the cgroup
cpuset and **not** by the inherited mask — so a launcher binding the rank
narrowly while `OMP_PLACES` scatters the threads wider is entirely legal, and an
earlier version reported `core -` for those threads. That is precisely the
Derecho shape.

### Output

Three prefixes, so any consumer can take just what it wants:

```bash
grep '^MPI rank' job.log                        # the human table
grep '^csv: '   job.log | cut -d' ' -f2- > p.csv # the spreadsheet
grep '^#'       job.log                          # provenance
```

```
# exe /container/bin/report_placement
# mpi Cray MPICH version 8.1.29
# thread_level requested=FUNNELED provided=MULTIPLE
# openmp 201811 procbind spread | ranks 8 | topology sysfs | mpiname nid001234
# env OMP_NUM_THREADS=8 OMP_PROC_BIND=spread OMP_PLACES=cores PBS_JOBID=1234.desched1
MPI rank 0/8 thread 0/8 host dec1234 cpu 0 core 0 socket 0 numa 0 l3 0 place 0 nallowed 8 affinity 0-7
csv: rank,nranks,thread,nthreads,host,cpu,core,socket,numa,l3,place,nallowed,affinity
csv: 0,8,0,8,dec1234,0,0,0,0,0,0,8,"0-7"
```

Design rules that matter if this is ever extended:

- **Every field is always present**; unavailable values print `-` (human) or
  empty (CSV). A dropped field shifts every downstream `awk $N`.
- **CSV uses empty, not `-`**, for missing values so spreadsheets keep the column
  numeric and sort it numerically.
- **`affinity` is quoted** in CSV — a mask straddling sockets looks like `0,64`
  and would otherwise split across two columns.
- **`nallowed` is the sortable scalar.** `1` = pinned; `128` = unbound. Sorting
  by it surfaces every unpinned thread at once. This is the field that
  distinguishes real pinning from a tidy-looking accident, since `cpu` alone
  looks perfectly sensible in a completely unbound job.
- **Output is gathered to rank 0** (`MPI_Gather` counts → `MPI_Gatherv` chars,
  once per block) so it is byte-identical run to run and a container run can be
  diffed against a native run. Per-thread printing interleaves *within* a line,
  and launcher stdout forwarding preserves no cross-rank order.
- **Everything printed is sanitized to plain text.** A single NUL makes GNU grep
  treat the whole stream as binary and refuse to print matches, which would
  silently break the `grep '^csv: '` workflow. Open MPI's
  `MPI_Get_library_version` reports a length that *includes* the terminating
  NUL — do not trust the reported length.

**`# mpi` is the payoff line on Derecho.** It names the MPI library that actually
loaded. Inside the container it says the container's own MPICH; through the Cray
ABI shim on a compute node it should name Cray MPICH. One line, no `ldd`.

## 2. `scripts/build_report-placement.sh`

Follows `build_osu-micro-benchmarks.sh` (same `build_common.cfg` sourcing header).

- Installs to `${INSTALL_ROOT}/bin/report_placement`, already on `PATH` via
  `config_env.sh`.
- Probes for the OpenMP flag by test-compiling: `-fopenmp` → `-mp` (nvhpc) →
  `-qopenmp` (older Intel).
- Reuses the `case "${MPI_FAMILY}"` block for `mpiexec_args` — OpenMPI needs
  `--map-by :OVERSUBSCRIBE` on a small CI runner.
- Then `report_cpu_features` followed by `mpiexec -n ${NRANKS:-2}`.
- `REPORT_PLACEMENT_RUN=0` compiles without launching, for use during
  `docker build`.

## 3. `containers/devenv/Dockerfile` (`final` stage)

One link in the existing `RUN` chain, after `COPY extras/` and before
`chown -R plainuser:`:

```dockerfile
    && ( command -v mpicxx >/dev/null 2>&1 \
         && REPORT_PLACEMENT_RUN=0 /container/extras/build_report-placement.sh \
         || echo "no mpicxx in this image; skipping report_placement" ) \
```

The `command -v mpicxx` guard matters: `FINAL_TARGET` is an ARG, so a non-MPI
image (`FINAL_TARGET=compilers`) must not fail the build.

## 4. `.github/workflows/build-hpc-dev-image-ghcr.yaml`

The `Test Image - Placement Report` step runs the build script inside the freshly
built image with `OMP_NUM_THREADS=2` and `NRANKS=2`. It rebuilds from source
(proving the on-Derecho rebuild path) and runs the result, with
`report_cpu_features` in between. Positioned before the OSU step so a broken MPI
stack fails fast.

---

## 5. Getting this onto Derecho

**Assumption:** Derecho's default/native MPI is **Cray MPICH** (Casper is the
OpenMPI cluster). Confirm with `echo $LMOD_FAMILY_MPI` on a login node.

OpenMPI containers are out of scope here — not from any fundamental limitation,
but because the mechanism is strictly harder on a Cray EX and MPICH gives a
working control first. The three differences for a later phase: (1) OpenMPI is
not part of the MPICH ABI Compatibility Initiative, so there is no
cross-implementation `lib-abi-mpich` equivalent — the container's OpenMPI must
*version-match* the host's, as the Casper script already does; (2) OpenMPI
bootstraps over PMIx and must be built against a PMIx compatible with what Cray
PALS serves, where MPICH's PMI-1 just works; (3) a container-native OpenMPI
driving the NIC would need libfabric's `cxi` provider matched to the host kernel
module. MPI 5.0's standardized ABI softens (1) once implementations ship it.

### Strategy A — build in the container, run against Cray MPICH (ABI shim)

This is what `PBS/OSU_derecho.pbs` already does, and what
`PBS/Placement_derecho.pbs` does for this tool. **Confirmed working on Derecho**
(2 nodes, `leap-gcc-mpich`, Cray MPICH 8.1.32.110).

#### ⚠ The `lib` vs `lib-abi-mpich` trap — read this first

Cray MPICH ships **two** library directories, and only one of them works:

| directory | contents | use it? |
|---|---|---|
| `${CRAY_MPICH_DIR}/lib` | the native Cray build, `libmpi_cray.so.12` | **no** |
| `${CRAY_MPICH_DIR}/lib-abi-mpich` | the ABI shim, `libmpi.so.12` with stock MPICH's symbol layout | **yes** |

Only `lib-abi-mpich` can impersonate the container's own MPICH. Point
`LD_LIBRARY_PATH` at `lib` and nothing supplies a competing `libmpi.so.12`, so
the container's own library wins.

**The failure is quiet and looks like a working job.** Every rank
singleton-initialises into its own `MPI_COMM_WORLD` of size 1 — you get N
processes, correctly distributed across nodes, each correctly pinned by PALS,
that never communicate. Diagnostic signature:

```
# mpi MPICH Version:      5.0.0        <- the CONTAINER's MPICH, not Cray's
MPI rank 0/1 ...                       <- repeated N times, always 0/1
```

versus a healthy run:

```
# mpi MPI VERSION    : CRAY MPICH version 8.1.32.110 (ANL base 3.4a2)
MPI rank 0/4 ... host dec0769
MPI rank 2/4 ... host dec0770
```

The `# mpi` header line makes this a one-line diagnosis. Without it the symptom
is easy to misread as a PMI or bind problem and chase for an afternoon.

Verify before running anything real — `libmpi.so.12` must resolve under
`/opt/cray`, not `/container`:

```bash
./apptainer-launch-*.sh ldd /container/bin/report_placement | grep -i mpi
```

If it still points into `/container/mpich/...`, also check whether the binary
carries a `DT_RPATH` (which beats `LD_LIBRARY_PATH`) rather than a `DT_RUNPATH`
(which does not): `readelf -d <exe> | grep -E 'RPATH|RUNPATH'`.

#### The rest of Strategy A

- **The container must be an MPICH variant** (`mpi: mpich` or `mpich3`). Cray
  MPICH is ABI-compatible with MPICH's `libmpi.so.12` only — an OpenMPI container
  cannot be shimmed and silently falls back to container-internal TCP.
- **The container does not fight you.** The MPICH stage registers its lib dir
  only in `/etc/ld.so.conf.d/` + `ldconfig`, never on `LD_LIBRARY_PATH`
  (`containers/devenv/Dockerfile:1091`). Since `LD_LIBRARY_PATH` is searched
  before `/etc/ld.so.cache`, a correct entry wins cleanly.
- **Bind `/run` *and* `/var/run`**, not just `/run/palsd` — PALS needs more than
  the socket. Binding all of `/opt/cray` is simpler than surgical per-directory
  binds and avoids missing a transitive dependency.
- **Do not hardcode PALS or libfabric version paths** (`/opt/cray/pals/1.6`,
  `/opt/cray/libfabric/1.22.0`). Resolve them at runtime, preferring
  `NCAR_ROOT_LIBFABRIC`; the versions move at system updates.
- The generated `apptainer-launch-${NCAR_HOST}-${LMOD_FAMILY_MPI}.sh` puts
  `${CRAY_MPICH_DIR}/lib-abi-mpich` first on `LD_LIBRARY_PATH`, binds `/opt/cray`,
  `/run`, `/var/run` (PALS) and `/usr/lib64:/host/lib64`, and sets
  `MPICH_SMP_SINGLE_COPY_MODE=NONE` (XPMEM/CMA need ptrace visibility the
  container namespace lacks).
- The **host** `mpiexec` (PALS) launches the ranks; each rank execs `apptainer`.
- Confirm the shim engaged: `export MPICH_VERSION_DISPLAY=1`, or just read the
  `# mpi` line. Add `MPICH_ENV_DISPLAY=1`, `MPICH_OFI_VERBOSE=1` when debugging.
- **Affinity is PALS's job.** Use `mpiexec --cpu-bind depth -d ${OMP_NUM_THREADS}`
  with `OMP_PROC_BIND=spread`, `OMP_PLACES=cores`.
- ⚠ **The `--cleanenv` trap.** `libexec/wrap_apptainer.sh` uses `--cleanenv`,
  which strips `OMP_NUM_THREADS` / `OMP_PROC_BIND` / `OMP_PLACES` at the container
  boundary — the *interactive* path silently loses your threading config. The
  PBS-generated launchers do not use it. The `# env` header line is what makes
  this visible.
- A container-built binary (especially `noble`, glibc 2.39) will not run bare on
  the Derecho host — only inside Apptainer.

### Strategy B — build natively in the Cray environment (the reference)

```bash
module --force purge && module load ncarenv/24.12 && module reset
module load craype cray-mpich gcc
CC -fopenmp -o report_placement_native scripts/report_placement.cxx
mpiexec -n 8 -ppn 4 --cpu-bind depth -d 4 ./report_placement_native
```

Same source, same output format → `diff` the two runs. A host-built binary can
also be run *inside* the container (`/glade` is already bound), isolating "MPI
stack" from "userspace ABI".

### Validation order

1. **GHA**: 2 ranks × 2 threads on a small runner. Cores collide; that is the
   expected baseline.
2. **Derecho, 1 node**: `select=1:ncpus=128:mpiprocs=4:ompthreads=8`,
   `mpiexec -n 4 --cpu-bind depth -d 8` → expect 4 disjoint 8-core blocks,
   `nallowed 8`, and each rank's threads sharing one `l3`.
3. **Derecho, 2 nodes**: `mpiexec -n 8 -ppn 4` → hostnames differ across the rank
   halves and `# mpi` names Cray MPICH.
4. **Compare** against the Strategy B native run.

Step 1 and a reduced form of step 3 are done (see *Verification*).
`PBS/Placement_derecho.pbs` implements steps 2 and 3 at full node width and adds
an unbound control case.

Still deferred: a Cray-native build script for Strategy B, and folding this
guidance into `NCAR_HowTo.md` — whose `Deffile` still bootstraps from Docker Hub
`ncarcisl/hpcdev-x86_64` rather than GHCR, so the deploy side has not been
migrated even though the images now publish to
`ghcr.io/<owner>/hpcdev-<arch>`.

---

## Files

| File | Change |
|---|---|
| `scripts/report_placement.cxx` | The diagnostic (renamed from `hello_world_mpi.cxx`, history preserved) |
| `scripts/build_report-placement.sh` | New build script |
| `scripts/hello_world_mpi.cxx` | Restored to its original trivial form |
| `src/report_placement.cxx` | New symlink → `../scripts/report_placement.cxx` |
| `containers/devenv/Dockerfile` | One line in the `final` stage |
| `containers/test/Dockerfile` | Smoke test now builds `report_placement` |
| `containers/demo/Dockerfile` | Unchanged — still builds `speak.exe` from the trivial hello world |
| `.github/workflows/build-hpc-dev-image-ghcr.yaml` | `Test Image - Placement Report` step |
| `.github/workflows/{devel-build-images,matrix-smoketest-applications,container-build}.yaml` | Renamed step + new source path |
| `README.md` | Regenerated the `container/extras/` listing |
| `.cspell.json` | Allowlist additions |
| `containers/deploy/ncar-hpc/PBS/Placement_derecho.pbs` | New — runs the tool on Derecho through the ABI shim, sweeping three bindings |

## Verification

**Verified** (Linux containers, aarch64):

- `report_placement` compiles under the production line (`mpicxx -fopenmp`, no
  `-std=`), `-std=c++11 -Wall -Wextra -pedantic`, `-std=c++23`, and **without**
  `-fopenmp`.
- Runs as a singleton with no `mpiexec` (the `containers/demo` path); exits 0
  under heavy oversubscription; byte-identical output across repeated runs.
- `nallowed` correctly distinguishes `--bind-to none` (`nallowed 12`) from
  `--bind-to core` (`nallowed 1`).
- Zero NUL bytes; both `grep` workflows produce clean output.
- Full `make extras` → `COPY extras/` → `final`-stage bake, plus the non-MPI
  path skipping cleanly rather than failing.
- The restored trivial `hello_world_mpi.cxx` still compiles and runs.

**Verified on Derecho** (2 nodes, `leap-gcc-mpich.sif`, Strategy A):

- The ABI shim engages and the header confirms it:
  `# mpi MPI VERSION : CRAY MPICH version 8.1.32.110 (ANL base 3.4a2)`.
- Ranks form a single `MPI_COMM_WORLD` across both nodes (`ranks 4`, ranks 0–1 on
  `dec0769`, 2–3 on `dec0770`).
- PALS `--cpu-bind depth` pins correctly (`nallowed 1` per rank).
- **`l3` populates on real hardware** — the last unverified code path. On an
  EPYC 7763, cpus 0 and 1 both report `l3 0`, which is correct: Milan puts cores
  0–7 on one L3.
- Cray MPICH granted `provided=FUNNELED` for the `FUNNELED` request.

**Not yet verified:**

- **x86_64 and non-GCC compilers.** All local testing was aarch64/GCC/OpenMPI;
  the Derecho run was x86_64/GCC/Cray-MPICH. CI is the first exercise of nvhpc,
  oneapi, aocc and clang.
- **Interesting placement.** The Derecho run used `ncpus=2`, so every rank landed
  on one chiplet, one socket, one NUMA node — `socket`, `numa` and `l3` were all
  constant and could not discriminate. `PBS/Placement_derecho.pbs` requests whole
  nodes precisely to fix this.
- **The `-d` argument to `--cpu-bind depth`.** A bare `--cpu-bind depth` is
  confirmed working; the explicit `-d N` in `Placement_derecho.pbs` is not.
- **Strategy B** (native Cray build) and the container-vs-native `diff`.

CI entry point:

```bash
gh workflow run dial-an-image-ghcr.yaml \
  -f os=almalinux9 -f compiler=os-gcc -f mpi=mpich -f test=true -f publish=false
```

Worth repeating with `mpi=openmpi`, `compiler=nvhpc` (most likely to trip the
OpenMP-flag probe and `omp_get_place_num`), and `os=almalinux8` (oldest system
gcc → the dialect check).
