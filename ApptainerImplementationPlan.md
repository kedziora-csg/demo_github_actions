# Hybrid MPI+OpenMP Hello World with per-thread core reporting

## Context

`scripts/hello_world_mpi.cxx` today prints one line per MPI rank with a *thread
count* — it never shows which core anything actually landed on, and its OpenMP
region runs before `MPI_Init`. That is not enough to answer the question we
actually care about on Derecho: **when we launch a containerized hybrid job
through PBS/PALS and Apptainer, does each rank/thread end up where we think it
does, and is the container really using Cray MPICH over the CXI fabric?**

This change turns the sample into a real placement diagnostic, bakes it into
every image at `/container/bin/hello_world_mpi`, and validates it in CI via
`build-hpc-dev-image-ghcr.yaml` (driven by `dial-an-image-ghcr.yaml`) before it
is ever used on Derecho. It also fixes an already-drafted-but-broken workflow
step that calls a `build_hello-world-mpi.sh` that does not exist.

Decisions already made:

- Output is **fully rank-ordered** (gathered to rank 0), so a container run and a
  Cray-native run can be `diff`'d directly.
- Binary is **baked into `/container/bin/`** by a reusable `build_*.sh` script.
- The per-thread **affinity mask is not printed** — only the CPU/core/socket the
  thread is running on. (See the note at the end of section 1: this is the one
  decision worth revisiting, and it is a small addition later.)
- Derecho support is **written guidance** (section 5); no new PBS or Cray build
  scripts in this change.

---

## 1. Rewrite `scripts/hello_world_mpi.cxx`

### Non-negotiable constraints

This one file has five consumers (`src/hello_world_mpi.cxx` is a symlink to it),
all compiling with exactly `mpicxx -o <exe> <src> -fopenmp` — no `-std=`, no
extra libs, no extra include paths:

| Call site | Note |
|---|---|
| `containers/test/Dockerfile:44` | `&&`-chained → **non-zero exit breaks the image build** |
| `containers/demo/Dockerfile:15,22` | builds `speak.exe` and runs it **bare, no `mpiexec`** → **singleton MPI init must work** |
| `.github/workflows/devel-build-images.yaml:161` | `&&`-chained, no `\|\| true` |
| `.github/workflows/matrix-smoketest-applications.yaml:99` | `&&`-chained, no `\|\| true` |
| `.github/workflows/container-build.yaml:94` | has `\|\| true` |

Therefore:

1. **Always exit 0.** Every degraded condition (no sysfs, oversubscription, low
   `provided` thread level) is *reported*, never fatal. A 2-core GH runner with
   `-n 2` × `OMP_NUM_THREADS=2` is guaranteed oversubscribed — that must not fail
   a build.
2. **No hwloc, no libnuma, no `-lpthread`.** Everything comes from
   `sched.h` / `syscall` / sysfs, which need only `_GNU_SOURCE`.
3. **Conservative dialect.** No `-std=` is passed, so the language level is
   whatever each compiler defaults to across the base-OS matrix (oldest system
   gcc in the matrix on the almalinux8/rockylinux8 bases, up through `nvc++` and
   `icpx` defaulting to C++17, and the gcc 15/16 source builds). **Write to C++11
   and avoid C++17-only spellings** — notably no non-const `std::string::data()`,
   no structured bindings, and keep `> >` spaced in nested templates. Verify by
   additionally compiling with `-std=c++11 -Wall -Wextra -pedantic` and
   `-std=c++23`, even though production passes neither.

### Structure

- `#ifndef _GNU_SOURCE / #define _GNU_SOURCE 1 / #endif` as the **first
  non-comment thing in the file**, with a loud comment. glibc latches
  feature-test macros in `<features.h>`, which the first system or libstdc++
  header pulls in; an include hoisted above it silently kills `sched_getcpu` and
  `sched_getaffinity`. The `#ifndef` avoids `-Wmacro-redefined` where the driver
  already defined it.
- `MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided)` as the **first
  statement in `main`**, with the OpenMP region strictly after it. Reasons:
  1. A thread pool created before MPI init binds against the *pre*-`MPI_Init`
     affinity, so anything the PMI bootstrap, the GTL `LD_PRELOAD`, or `--nv` GPU
     init does to the process is invisible — we want the state the compute phase
     sees.
  2. `provided` is itself a headline diagnostic, since container-MPICH and host
     Cray-MPICH via the ABI shim can report different levels.
  3. `FUNNELED` is the honest contract (only the master thread calls MPI) and
     avoids selecting Cray-MPICH's slower thread-safe path that `MULTIPLE` would.

  Print `provided` symbolically (`SINGLE`/`FUNNELED`/`SERIALIZED`/`MULTIPLE`);
  **do not abort** when it is below what was requested.
- **Current CPU + NUMA node:** `syscall(SYS_getcpu, &c, &n, NULL)` as the
  *primary* — no glibc-version dependency, present on every Linux/arch this will
  see, and it returns the NUMA node for free. `sched_getcpu()` is the fallback
  (guarded by a **nested** `__GLIBC_PREREQ(2,6)` test — `#if defined(X) && X(...)`
  can hard-error on the unexpanded token). Do *not* use libc `getcpu()` (glibc
  ≥ 2.29 only).
- **Logical CPU → physical core / socket, from sysfs**
  (`/sys/devices/system/cpu/cpu<N>/topology/`):
  - `physical_package_id` → socket.
  - `core_id` → **unique only within a package**; on Zen3 it is APIC-derived and
    sparse. Never dedupe on it alone.
  - `thread_siblings_list` → its **first element is a globally unique physical
    core id**, and its length is an SMT detector (1 ⇒ SMT off, which is what
    Derecho should show). This is the no-hwloc equivalent of an hwloc core
    object, and it is what the `core` column should report.
  - Read this **once on the main thread before any threading**, and only for the
    CPUs in the process affinity mask (typically 1–16 per rank), so 128 ranks on
    a node don't do ~49k redundant `open()`s. Keeps `ifstream` out of the
    parallel region entirely.
  - sysfs is visible in both Docker (read-only real sysfs) and Apptainer
    (`mount sys = yes` is the `apptainer.conf` default; neither
    `wrap_apptainer.sh` nor the PBS launchers disable it), and it is **not**
    cgroup-filtered — it shows all 128 node CPUs even under a 16-CPU cpuset,
    which is what we want. Degrade to `-` (never crash) under `--contain`,
    `--no-mount sys`, gVisor/Kata, or arches missing `physical_package_id`.
- **OpenMP, correctly guarded.** `#ifdef _OPENMP` — note the sibling
  `scripts/hello_world.cxx:15` has a `#ifdef _OPENNMP` typo (triple N), so its
  parallel region is dead code and has *never* run. Do not copy that. Route every
  `omp_*` call through privately-named static shims (`hw_thread_num()`,
  `hw_num_threads()`) that return `0`/`1` without OpenMP, so the no-`-fopenmp`
  build is the *same* code path, not a second branch. Version-gate the newer
  queries on `_OPENMP`'s value: `omp_get_proc_bind()` needs ≥ `201307` (4.0),
  `omp_get_place_num()` needs ≥ `201511` (4.5); both fall back to `-`.
- **Set nothing.** No `omp_set_num_threads()`, no `num_threads(...)`, no
  `proc_bind(...)` clause, no `omp_set_dynamic()`, no `setenv`. The tool's entire
  value is being a passive observer of what `OMP_NUM_THREADS` / `OMP_PROC_BIND` /
  `OMP_PLACES` / PBS `ompthreads` / PALS produced. Thread count comes from
  `omp_get_num_threads()` *inside* the region (the actual team size), never
  `omp_get_max_threads()` outside it — those disagree under `OMP_DYNAMIC`,
  `OMP_THREAD_LIMIT`, or cpuset clamping, which are exactly the situations this
  tool exists to find.

### Ordering — the part that makes it diff-able

1. A throwaway empty `#pragma omp parallel { }` warm-up first, so the pool exists
   and binding has settled before we sample it.
2. Real region: `#pragma omp single` sets `nthreads = hw_num_threads()` and
   `lines.resize(nthreads)` (the implicit barrier is the flush point); then each
   thread writes its own record into `lines[hw_thread_num()]`. Concurrent
   assignment to *distinct* elements of a non-resizing vector is well-defined —
   no locking, no `critical`.
3. Concatenate to one `std::string` per rank, copy into a `std::vector<char>`
   (avoids the C++17 non-const `std::string::data()`), and gather:
   `MPI_Gather` the per-rank byte counts → build displacements on rank 0 →
   `MPI_Gatherv` of `MPI_CHAR` → rank 0 is the sole writer of stdout.
   Handles ragged data naturally (different thread counts, different hostname
   lengths). Allocate `counts`/`displs` on **all** ranks and never pass a null
   `sendbuf` even at length 0 — some implementations dereference them regardless
   of what the standard says.

Direct per-thread printing was rejected: `operator<<` chains are many separate
writes so lines interleave *within* themselves, and PALS stdout forwarding
preserves atomicity only up to `PIPE_BUF` with no cross-rank ordering. A barrier
token ring orders the `write()` calls but not the launcher's forwarding of them.

### Output format

Record-tagged, unpadded, single-space separated, `-` for any unavailable value —
**never drop a field**, or every downstream awk `$N` shifts silently.

```
# exe /container/bin/hello_world_mpi
# mpi <MPI_Get_library_version first line> | thread_level requested=FUNNELED provided=MULTIPLE
# openmp 201811 procbind spread | ranks 8 | topology sysfs
# env OMP_NUM_THREADS=4 OMP_PROC_BIND=<unset> OMP_PLACES=<unset> PBS_JOBID=1234.desched1
# columns: thrd rank r/nranks thread t/nthreads host <n> cpu <i> core <i> socket <i> numa <i> place <i>   ('-' = unavailable)
thrd rank 0/8 thread 0/4 host dec0123 cpu 0 core 0 socket 0 numa 0 place 0
thrd rank 0/8 thread 1/4 host dec0123 cpu 1 core 1 socket 0 numa 0 place 1
```

- `#`-prefixed header lines so `grep -v '^#'` / `awk '$1=="thrd"'` are exact —
  these land in PBS logs interleaved with module output, `set -x` traces, and
  MPICH warnings.
- **Echo the raw `OMP_*` `getenv()` values as seen *inside* the container.** This
  is the cheap detector for the `--cleanenv` trap in section 5.
- `place` (`omp_get_place_num()`, `-1`/`-` when unbound) is the direct readout of
  `OMP_PLACES` binding — it carries most of the "is this actually pinned?" signal
  without printing a mask.
- Also call `MPI_Get_processor_name` alongside `gethostname`; where the two
  disagree is diagnostic (Apptainer shares the host UTS namespace so
  `gethostname()` gives `dec1234`; Docker's private UTS gives a container id).
  Guard `MPI_Get_library_version` with
  `#if defined(MPI_VERSION) && MPI_VERSION >= 3` and heap-allocate its
  8192-byte buffer.

**Deliberately cut to keep this ~200 lines rather than ~350:** the per-thread
`sched_getaffinity` mask column, and a rank-0 `# summary` line (second numeric
gather → detect two threads on one core). Worth knowing before you finalize: the
design review argued the mask is the single field that distinguishes "PALS
`--cpu-bind` worked" from "everything is free-floating and will migrate the
moment the node gets busy" — `cpu` alone looks perfectly sane in the unbound
case, because the kernel's initial placement is tidy. Concretely on Derecho you'd
read it as:

- `naff 1` ⇒ pinned to one core (what `ompthreads=1` + `--cpu-bind depth` gives)
- `naff 4, affinity 0-3` ⇒ rank bound to a 4-core span, threads *not*
  individually bound (`OMP_PROC_BIND` unset)
- `naff 128, affinity 0-127` ⇒ **unbound**, however pretty `cpu` looks

`place` is a partial substitute. Adding the mask later is `sched_getaffinity(0, …)`
(pid 0 = the *calling thread* on Linux, which is the whole point — do not "fix"
it to `getpid()`) + `CPU_ISSET` + a range-list formatter: ~25 lines and one extra
column.

---

## 2. New `scripts/build_hello-world-mpi.sh`

Model on `scripts/build_osu-micro-benchmarks.sh` — reuse its `SCRIPTDIR` /
`build_common.cfg` sourcing header (lines 3–10) verbatim.

- Install target `${INSTALL_ROOT}/bin/hello_world_mpi` →
  `/container/bin/hello_world_mpi`, already on `PATH` via `config_env.sh`
  (`containers/devenv/Dockerfile:96`).
- Source resolution: `${SCRIPTDIR}/hello_world_mpi.cxx`, falling back to
  `/container/extras/hello_world_mpi.cxx` (mirrors the `build_common.cfg` idiom).
- OpenMP flag: probe by test-compiling a trivial TU — `-fopenmp`, then `-mp`
  (nvhpc), then `-qopenmp` (older Intel). All current families accept `-fopenmp`,
  but the probe costs nothing and removes a standing footgun.
- Reuse the `case "${MPI_FAMILY}"` block from
  `scripts/build_osu-micro-benchmarks.sh:32-40` for `mpiexec_args` — OpenMPI
  needs `--map-by :OVERSUBSCRIBE` for 2 ranks × 2 threads on a 2-core GH runner.
- After building: `report_cpu_features ${exe}` (the existing `/container/bin/`
  tool, `containers/devenv/Dockerfile` FILE8 heredoc, lines 250–314), then
  `mpiexec -n ${NRANKS:-2} ${mpiexec_args} ${exe}`.
- Gate the run phase behind `HELLO_WORLD_MPI_RUN=${HELLO_WORLD_MPI_RUN:-1}` so
  the Dockerfile can compile without launching MPI inside `docker build`.

Ships to `/container/extras/` automatically — `containers/devenv/Makefile` does
`cp ../../scripts/*.* extras/` and the glob matches.

---

## 3. Bake into the image: `containers/devenv/Dockerfile`

In the `final` stage (lines 1516–1527), after `COPY extras/ /container/extras/`
and **before** `chown -R plainuser:`, add one link to the existing `RUN` chain:

```dockerfile
    && ( command -v mpicxx >/dev/null 2>&1 \
         && HELLO_WORLD_MPI_RUN=0 /container/extras/build_hello-world-mpi.sh \
         || echo "no mpicxx in this image; skipping hello_world_mpi" ) \
```

The `command -v mpicxx` guard matters: `FINAL_TARGET` is an ARG, so a non-MPI
image (`FINAL_TARGET=compilers`) must not fail the build. For the default
`FINAL_TARGET=fftlibs`, `mpicxx` is guaranteed because the `mpi` stage
(`Dockerfile:1157`) is upstream.

No new stage, no new `COPY` — this rides the one existing `COPY` in the file.

---

## 4. Fix the CI test step: `.github/workflows/build-hpc-dev-image-ghcr.yaml`

The uncommitted step at lines 302–308 calls a script that does not exist yet
(step 2 creates it) and passes a stray `OMB_VERSION` copied from the OSU step.
Replace with:

```yaml
      - name: Test Image - Hello World MPI
        if: ${{ inputs.test }}
        run: |
          docker run --rm --env-file test_env.cfg \
            --env OMP_NUM_THREADS=2 \
            --env NRANKS=2 \
            ${{ env.CI_TAG }} \
            /container/extras/build_hello-world-mpi.sh
```

This exercises both paths at once — it rebuilds from source (proving the script
works for an on-Derecho rebuild) and runs the result under `mpiexec`, with
`report_cpu_features` in between. Keep it before the OSU step so a broken MPI
stack fails fast and cheaply.

Trigger via `.github/workflows/dial-an-image-ghcr.yaml`, the only caller of this
reusable workflow.

---

## 5. Getting this onto Derecho — the two build strategies

*(Guidance only; no files in this change. Fold into
`containers/deploy/ncar-hpc/NCAR_HowTo.md` once validated.)*

**Assumption:** Derecho's default/native MPI is **Cray MPICH** (Casper is the
OpenMPI cluster; cf. `OSU_derecho.pbs` vs. `OSU_casper.pbs` in this repo).
Confirm on a login node with `echo $LMOD_FAMILY_MPI` before the first run.

OpenMPI containers are deliberately **out of scope here** — not because of any
fundamental limitation, but because the mechanism is strictly harder on a Cray
EX and MPICH gives us a working control first. For the record, the three
differences to pick up in a later phase: (1) OpenMPI is not part of the MPICH ABI
Compatibility Initiative, so there is no cross-implementation `lib-abi-mpich`
equivalent — the container's OpenMPI must *version-match* the host's, as the
Casper script already does; (2) OpenMPI bootstraps over PMIx and must be built
against a PMIx compatible with what Cray PALS serves, where MPICH's PMI-1 just
works; (3) a container-native OpenMPI driving the NIC would need libfabric's
`cxi` provider matched to the host kernel module. MPI 5.0's standardized ABI
softens (1) once implementations ship it.

### Strategy A — build inside the container, run against Cray MPICH (ABI shim)

This is what `containers/deploy/ncar-hpc/PBS/OSU_derecho.pbs` already does;
`/container/bin/hello_world_mpi` slots straight in.

- **The container must be an MPICH variant** (`mpi: mpich` or `mpich3`). Cray
  MPICH is ABI-compatible with MPICH's `libmpi.so.12` only — an OpenMPI container
  cannot be shimmed and will silently fall back to container-internal TCP, losing
  the fabric.
- The generated `apptainer-launch-${NCAR_HOST}-${LMOD_FAMILY_MPI}.sh`
  (`OSU_derecho.pbs:28-53`) puts `${CRAY_MPICH_DIR}/lib-abi-mpich` first on
  `LD_LIBRARY_PATH`, binds `/opt/cray`, `/run`, `/var/run` (PALS), and
  `/usr/lib64:/host/lib64`, and sets `MPICH_SMP_SINGLE_COPY_MODE=NONE` (XPMEM/CMA
  need ptrace visibility the container namespace lacks).
- The **host** `mpiexec` (PALS) launches ranks; each rank execs `apptainer`. The
  container never runs its own launcher.
- Reuse the existing "ldd container-native vs. ldd host-binds" diff
  (`OSU_derecho.pbs:58-73`) pointed at `/container/bin/hello_world_mpi` —
  `libmpi` must resolve under `/opt/cray`, not `/container`.
- Strongest single check that the shim took: `export MPICH_VERSION_DISPLAY=1`
  (plus `MPICH_ENV_DISPLAY=1`, `MPICH_OFI_VERBOSE=1` when debugging). If the
  banner says *Cray MPICH*, you are on the fabric.
- **Affinity is PALS's job, not the container's.** Use
  `mpiexec --cpu-bind depth -d ${OMP_NUM_THREADS}` with `OMP_PROC_BIND=spread`,
  `OMP_PLACES=cores`.
- ⚠ **The `--cleanenv` trap.** `containers/deploy/ncar-hpc/libexec/wrap_apptainer.sh:37,50`
  uses `--cleanenv`, which strips `OMP_NUM_THREADS` / `OMP_PROC_BIND` /
  `OMP_PLACES` (and PMI vars) at the container boundary — so the *interactive*
  path silently loses your threading config. The PBS-generated launchers do not
  use it, so the batch path is fine. This divergence is a classic "why is the
  container slower than native" cause, and the `# env` header line in section 1
  is exactly what makes it visible. Forward with `--env` / `APPTAINERENV_` where
  needed.
- A container-built binary (especially `noble`, glibc 2.39) will **not** run bare
  on the Derecho host — only inside Apptainer. That is why Strategy A always goes
  through the launcher.
- **Singleton init:** verify `MPI_Init_thread` with no `mpiexec` works under
  Cray-MPICH-via-shim, not just container MPICH — Cray MPICH historically wants
  PMI present. (`containers/demo/Dockerfile:22` depends on this too.)

### Strategy B — build natively in the Cray environment (the reference)

```bash
module --force purge && module load ncarenv/24.12 && module reset
module load craype cray-mpich gcc          # or intel / nvhpc
CC -fopenmp -o hello_world_mpi_native scripts/hello_world_mpi.cxx   # Cray C++ wrapper
mpiexec -n 8 -ppn 4 --cpu-bind depth -d 4 ./hello_world_mpi_native
```

Same source, same output format → `diff` the two runs. This is the ground truth
for "what correct placement looks like on this node type." A host-built binary
*can* additionally be run inside the container (`/glade` is already bound) since
the container glibc is newer than the host's — a third data point isolating "MPI
stack" from "userspace ABI."

### Suggested validation order

1. **GHA** (this change): 2 ranks × 2 threads on a 2–4 core runner. Cores will
   collide; that is the expected, informative baseline.
2. **Derecho, 1 node**: `select=1:ncpus=128:mpiprocs=4:ompthreads=8`,
   `mpiexec -n 4 --cpu-bind depth -d 8` → expect 4 disjoint 8-core blocks, one
   thread per core, sockets 0/1 split cleanly, `place` populated.
3. **Derecho, 2 nodes**: `select=2:...`, `mpiexec -n 8 -ppn 4` → hostnames must
   differ across the rank halves and `MPICH_VERSION_DISPLAY` must say Cray.
4. **Compare** against the Strategy B native run.

Deliberately deferred: `PBS/HelloWorld_derecho.pbs` and a Cray-native build
script under `containers/deploy/ncar-hpc/`. Add them once steps 2–3 are confirmed
by hand — at that point they are a near-copy of `OSU_derecho.pbs`.

---

## Files to change

| File | Change |
|---|---|
| `scripts/hello_world_mpi.cxx` | Rewrite, ~200 lines (section 1) |
| `scripts/build_hello-world-mpi.sh` | New (section 2) |
| `containers/devenv/Dockerfile` (~line 1517) | One line in the `final` stage (section 3) |
| `.github/workflows/build-hpc-dev-image-ghcr.yaml` (~line 302) | Fix the drafted test step (section 4) |
| `.cspell.json` | Add `sysfs`, `getcpu`, `numa`, `apptainer`, `derecho`, `ompthreads`, `mpiprocs`. `scripts/*.cxx` *is* spell-checked, but MegaLinter runs `DISABLE_ERRORS: true`, so this is noise-reduction only, not a gate. |

Not touched: `containers/test/Dockerfile`, `containers/demo/Dockerfile`, and the
three other workflows — they keep compiling the source directly and inherit the
new output for free. `src/hello_world_mpi.cxx` is a symlink, so it follows.

---

## Verification

**Local — dialect and degraded paths first, since those fail on the oldest image
in the matrix and nowhere else:**

```bash
mpicxx -std=c++11 -Wall -Wextra -pedantic -fopenmp -o /tmp/hw scripts/hello_world_mpi.cxx
mpicxx -std=c++23 -Wall -Wextra           -fopenmp -o /tmp/hw scripts/hello_world_mpi.cxx
mpicxx                                              -o /tmp/hw_noomp scripts/hello_world_mpi.cxx  # no -fopenmp
/tmp/hw_noomp                       # singleton, no mpiexec -- must print 1 thrd line, exit 0
OMP_NUM_THREADS=4 mpiexec -n 2 /tmp/hw > a.txt
OMP_NUM_THREADS=4 mpiexec -n 2 /tmp/hw > b.txt && diff a.txt b.txt   # must be identical
echo $?                             # must be 0 even when oversubscribed
```

**CI — the real gate:**

```bash
gh workflow run dial-an-image-ghcr.yaml \
  -f os=almalinux9 -f compiler=os-gcc -f mpi=mpich -f test=true -f publish=false
gh run watch
```

Repeat with `mpi=openmpi`, `compiler=nvhpc` (most likely to trip the OpenMP-flag
probe and `omp_get_place_num`), and `os=almalinux8` (oldest system gcc → the
dialect check). Confirm in the `Test Image - Hello World MPI` log: clean compile,
`report_cpu_features` output, `#` headers with the `OMP_*` env echo, and 4
ordered `thrd` lines.

**Then Derecho**, following the validation order in section 5.
