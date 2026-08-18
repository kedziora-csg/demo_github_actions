# Plan: What To Send Upstream, And What To Keep In The Fork

Status: **PR 1 is submitted upstream as benkirk#35 (open, awaiting review). PR 2 is still
a proposal.**

Handoff note for a future session. It records the fork/upstream split decision while it is
fresh, so the next session starts from a decision instead of re-deriving one.

Companion to [BenchmarkRunnerPlan.md](BenchmarkRunnerPlan.md); see §5 for how the two
interact. Short version: **§3 is done; neither PR is a prerequisite for benchmark work.**

Revised 2026-08-18. The oneAPI direction reversed since the first draft — see §3 — so §3
and §4 were rewritten. §2 Bucket B, §5 and most of §6 are unchanged. Updated again once
PR 1 was submitted; §3 gained "Consequence for `build_hpcg.sh`".

---

## 1. Facts

Measured 2026-08-18, after `git fetch upstream`.

| | |
|---|---|
| Fork | `kedziora-csg/demo_github_actions` (`origin`) |
| Upstream | `benkirk/demo_github_actions` (`upstream`), default branch `main` |
| Divergence | **78 commits ahead, 0 behind** `upstream/main` |
| Merge base | `f471750` — "June refresh (#31)", 2026-06-26 |
| Total diff | 44 files, +8118 / −50 |
| PR target | `benkirk:main` — upstream merges PRs there (`#31`) |
| Open PR | **benkirk#35** — `kedziora-csg:oneapi-2026.1.0` → `main`, opened 2026-08-18, 3 files +70 / −10 |
| Other upstream branches | `cuda13`, `comprehend/workflow-dependency-auto-refresh` |

Two consequences worth noting:

- **Zero commits behind** means any topic branch cut from `upstream/main` applies without a
  rebase, and there is no upstream drift creating time pressure. Upstream has been idle for
  eight weeks.
- We also hold `comprehend/workflow-dependency-auto-refresh` **8 ahead** of upstream's copy
  of the same branch. That is a separate collaboration track with its own history; it is
  out of scope here and should not be folded into either PR below.

---

## 2. The split

Roughly 1,800 of the 8,002 added lines are plausibly upstream's. The rest is fork
infrastructure and design work.

### Bucket A — send upstream

| Change | Files | Why it is upstream's |
|---|---|---|
| **oneAPI 2026.1.0 bump + compiler guard** | `containers/devenv/Dockerfile` (oneapi stage), `.github/workflows/{devel-build-images,matrix-build-images}.yaml` | Upstream's 2026.0.0 default miscompiles HPCG under OpenMP; the guard catches a silent half-install |
| **`report_placement` smoke diagnostic** | `scripts/report_placement.cxx`, `src/report_placement.cxx`, `scripts/build_report-placement.sh`, `containers/test/Dockerfile`, `.github/workflows/{container-build,devel-build-images,matrix-smoketest-applications,build-hpc-development-image}.yaml`, `containers/devenv/Dockerfile` (final stage) | Strictly better image validation, applies to every image upstream builds |

### Bucket B — keep in the fork

| Change | Lines | Why it stays |
|---|---|---|
| `*-ghcr.yaml` — `build-hpc-dev-image`, `matrix-build-images`, `dial-an-image`, `derecho-images`, `hpcg-smoketest` | ~1,110 | Target `ghcr.io/kedziora-csg`; upstream publishes to DockerHub `ncarcisl`/`benjaminkirk`. Fork registry plumbing. |
| `containers/deploy/ncar-hpc/**` (17 files) | ~1,000 | The Apptainer/PBS/placement harness. Moving fast, and pre-contract — see §5. |
| `containers/apps/hpcg/`, `scripts/build_hpcg.sh` | ~280 | App layer. `SeparationAnalysis.md` argues this separates *out* of the factory, not into it. |
| `README.md` (+645), `README_old.md`, `DockerfileDoc.md`, `SeparateConcerns.md`, `SeparationAnalysis.md`, `BenchmarkRunnerPlan.md`, this file | ~2,400 | Fork docs and design work |
| `.gitignore`, `.cspell.json` | 12 | Fork-local; `.cspell.json` words ride along with whichever PR needs them |

### Noise — drop from any PR

- `containers/devenv/README.md` deletes a one-line sentence for no stated reason. Restore
  it before branching; it will read as an accidental deletion in review.
- `ApptainerImplementationPlan.md` was added (+570) and later removed (`5e60cbe`). It
  inflates the diffstat of `a1df5f3` but no longer exists. Ignore it.

---

## 3. Resolved: the oneAPI scheme reversed, and the build-args agree again

**This section was the blocker. It is done.** What follows is the record of what changed
and why, because the reasoning is not recoverable from the diff.

### The reversal

The first draft proposed collapsing the two per-language oneAPI installer URLs into a
single `ONEAPI_TOOLKIT_URL` (commits `35d6f51`, `3d5e2de`). That direction was abandoned on
2026-08-18 and the **two-URL component scheme is back**:

- `ONEAPI_CC_URL` — `intel-dpcpp-cpp-compiler-*`, supplies `icx`/`icpx`
- `ONEAPI_FC_URL` — `intel-fortran-compiler-*`, supplies `ifx`

Reasons, in order of weight:

1. **Upstream never followed.** `upstream/main` still declares `ONEAPI_CC_URL` +
   `ONEAPI_FC_URL` at 2026.0.0. The toolkit collapse was a fork-only detour, so reverting it
   makes the PR *smaller* — the scheme is no longer part of the diff at all.
2. **The toolkit default was never tested.** `2026.1.0` was originally picked because it was
   the installer on Intel's download page that paired with the 2025.3.2 components, not
   because anything had been run against it.
3. **URL provenance now has a source of truth.** Spack's `intel_oneapi_compilers`
   `package.py` carries a `cpp` and an `ftn` URL per release. Intel embeds an opaque
   per-release UUID that cannot be derived from the version number, so a bump means copying
   both URLs from Spack — editing the version inside an existing URL yields a 404. Recorded
   in the Dockerfile comment.
4. **Disk.** Two components are fetched, installed and deleted one at a time (1.0 GB +
   0.8 GB, never coexisting); the merged toolkit is a single 2.6 GB download.

### The half-migration is gone

The four-consumer table in the previous draft — three workflows passing undeclared
build-args while stamping a version they were not installing — no longer applies. The
Dockerfile declares both ARGs again and every consumer passes them:

| Workflow | Passes | Declared? | Installs | Stamps |
|---|---|---|---|---|
| `matrix-build-images.yaml` | CC+FC URLs | yes | 2026.1.0 | 2026.1.0 ✅ |
| `devel-build-images.yaml` | CC+FC URLs | yes | 2026.1.0 | 2026.1.0 ✅ |
| `matrix-build-images-ghcr.yaml` | CC+FC URLs | yes | 2026.1.0 | 2026.1.0 ✅ |
| `derecho-images-ghcr.yaml` | CC+FC URLs, from a dispatch input | yes | selected | selected ✅ |

The `CLAUDE.md` invariant — the CI matrices are the authoritative versions — holds again,
so `BenchmarkRunnerPlan.md` §7 provenance rows can trust in-image `ONEAPI_VERSION`.

### DECISION A — answered: 2026.1.0 everywhere

Not a preference. **oneAPI 2026.0.0, upstream's current default, miscompiles HPCG.** On
Derecho, in the `leap-oneapi-*` images:

```
xhpcg: CheckProblem.cpp:146: Assertion `A.totalNumberOfNonzeros == totalNumberOfNonzeros' failed.
```

OpenMP threading is the common factor: it fails in hybrid MPI+OpenMP *and* in pure OpenMP on
a single rank, so MPI is not involved. At one rank no MPI reduction feeds that number — HPCG
accumulates `localNumberOfNonzeros` inside an OpenMP-parallel region in `GenerateProblem` —
so the threaded count itself is wrong. Clean bare-metal, and clean under the other compilers
in the same image set.

**2026.1.0 clears it, verified on Derecho.** 2025.3.2 is the last pre-2026 release known
good and is kept as the fallback option in `derecho-images-ghcr.yaml`, which grew a
`oneapi_version` dispatch input so a suspect release can be A/B'd without editing a file.

### The guard

The Dockerfile now checks `icx`, `icpx` and `ifx` immediately after `setvars.sh`. With two
independent installers either can fail to deliver its compiler without failing the build:
`which` returns empty, that `CC`/`CXX`/`FC` is exported empty, and autoconf and cmake fall
through to the base image's GNU toolchain, present on every `base_os`. The quiet direction
is the dangerous one:

- **no `ifx`** — dies much later linking Intel-compiled C against a GNU-driven `mpifort`.
- **no `icx`** — does not die at all. gcc-built C and ifx-built Fortran link happily, so the
  build goes green and publishes a GNU-built image labelled `oneapi`.


**Where the history lives now.** On 2026-08-18 the 2026.0.0 narrative was deliberately
stripped out of the code: the Dockerfile oneapi comment, the three matrix workflows, and
`derecho-images-ghcr.yaml` no longer explain *why* 2026.1.0 is the default, and
`build_hpcg.sh`'s patch comment no longer recounts the investigation. Solved issues leave
tracks in git history, in this document, and in PR benkirk#35's description — not in
comments a future reader has to wade through. What stayed in the code is only what is still
actionable: both installers are required, the URLs come from Spack and carry a
non-derivable UUID, the components are ~1.3 GB in a SIF against the toolkit's ~3.8 GB, and
a missing compiler falls through to GNU silently (hence the guard). The bump procedure is
now recorded once, in `CLAUDE.md` under "Versions and how to bump them".

Note this leaves **PR benkirk#35's branch carrying the older, verbose comment**, which is
appropriate — a reviewer with no other context needs the justification for the bump — but it
means a fork-local trim hunk reappears on `containers/devenv/Dockerfile` if and when #35
merges.

This matters beyond tidiness: `BenchmarkRunnerPlan.md` phase 2 moves `build_hpcg.sh`'s
app-specific knowledge into `/container/app.d/hpcg/`, so an incorrect root-cause note gets
carried into the app contract unless it is corrected first.

---

## 4. The two PRs

### PR 1 — oneAPI: bump to 2026.1.0, and fail loudly when a compiler is missing

**Status: SUBMITTED.** [benkirk#35](https://github.com/benkirk/demo_github_actions/pull/35),
opened 2026-08-18 — base `main`, head `kedziora-csg:oneapi-2026.1.0` @ `9873d9d`, 3 files
+70 / −10, open and awaiting review. Scope verified against the submitted diff: it contains
no `report_placement` lines, so PR 2 stayed out of it.

**Do not cherry-pick.** `35d6f51` and `3d5e2de` implement the toolkit collapse — the
opposite of what shipped — so replaying them applies a change that then has to be reverted.
The branch was built from the *final tree*:

```bash
git worktree add <scratch>/pr-oneapi -b oneapi-2026.1.0 upstream/main   # dirty main untouched
cp <the 3 files from main's working tree> <scratch>/pr-oneapi/
# strip the report_placement hunks -- the Dockerfile final stage, and the
# devel-build-images.yaml hello_world -> report_placement rename.  Those are PR 2.
git add containers/devenv/Dockerfile .github/workflows/{devel,matrix}-build-images.yaml
git commit
```

A worktree rather than `git checkout -b` because `main` carries uncommitted work that
`checkout` would drag onto the new branch. The branch lives in the shared `.git`, so it
survives removing the worktree directory.

**Scope — 3 files, 4 hunks**, verified with `git diff --stat upstream/main` (that diff *is*
the PR):

| File | Hunks |
|---|---|
| `containers/devenv/Dockerfile` | ARG block + comments; the `icx`/`icpx`/`ifx` guard |
| `.github/workflows/matrix-build-images.yaml` | oneapi include entry |
| `.github/workflows/devel-build-images.yaml` | oneapi include entry |

Deliberately excluded: `report_placement` (PR 2) and both `*-ghcr.yaml` workflows (Bucket B).

Submitted with:

```bash
git push -u origin oneapi-2026.1.0
gh pr create --repo benkirk/demo_github_actions \
  --base main --head kedziora-csg:oneapi-2026.1.0 \
  --title "oneapi: bump to 2026.1.0, and fail the build when a compiler is missing"
```

Proposed description — the durable copy; a longer draft lives in session scratch, which is
ephemeral:

> **What** — bumps the oneAPI compilers from 2026.0.0 to 2026.1.0, and adds an
> `icx`/`icpx`/`ifx` presence check to the oneapi stage.
>
> **Why the bump** — HPCG built with the current 2026.0.0 default aborts on NCAR's Derecho
> at `CheckProblem.cpp:146`, `Assertion 'A.totalNumberOfNonzeros == totalNumberOfNonzeros'`.
> OpenMP threading is the common factor: it fails in hybrid MPI+OpenMP and in pure OpenMP on
> a single rank, so MPI is not implicated — at one rank no MPI reduction feeds that number,
> and HPCG accumulates `localNumberOfNonzeros` inside an OpenMP-parallel region in
> `GenerateProblem`, so the threaded count itself is wrong. Clean bare-metal and clean under
> the other compilers in the same image set. 2026.1.0 clears it, verified on Derecho.
>
> **Why the guard** — the stage installs two independent component installers; if either
> fails to deliver its compiler the build does not fail. `CC`/`CXX`/`FC` is exported empty
> and autoconf/cmake fall through to the base image's gcc/g++/gfortran. A missing `ifx` dies
> later linking against a GNU-driven `mpifort`; a missing `icx` does not die at all and
> publishes a GNU-built image labelled `oneapi`. The guard fails immediately, naming the
> installer at fault.
>
> **URL provenance** — the URLs are Spack's, from the `cpp`/`ftn` entries of
> `intel_oneapi_compilers/package.py`. Intel's per-release UUID cannot be derived from the
> version number, so a bump means copying both fresh URLs from there.

Low risk, self-contained, fixes a live miscompile in upstream's default. Send this first.

### PR 2 — `report_placement`: a placement-aware replacement for the MPI hello-world

**Do not cherry-pick.** The change is smeared across three commits that each mix Bucket A
and Bucket B:

| Commit | Bucket A content | Bucket B content also present |
|---|---|---|
| `34421b2` | Dockerfile hook, the `.cxx` growth (+538) | `build-hpc-dev-image-ghcr.yaml`, `.cspell.json` |
| `a1df5f3` | rename + CSV/L3/affinity, `containers/test/Dockerfile`, 3 workflows | `ApptainerImplementationPlan.md` (±570), `README.md`, ghcr workflow |
| `47b435f` | `report_placement.cxx` (+19) | the entire Derecho harness, `build_hpcg.sh` |

Build the branch from the **final tree** instead, which sidesteps the untangling entirely:

```bash
git checkout -b upstream/report-placement upstream/main
git checkout main -- \
    scripts/report_placement.cxx \
    src/report_placement.cxx \
    scripts/build_report-placement.sh \
    containers/test/Dockerfile \
    .github/workflows/container-build.yaml \
    .github/workflows/devel-build-images.yaml \
    .github/workflows/matrix-smoketest-applications.yaml \
    .github/workflows/build-hpc-development-image.yaml
# hand-apply ONLY the report_placement hunk of containers/devenv/Dockerfile (~line 1529)
# revert the devel-build-images.yaml oneAPI hunk -- that belongs to PR 1, not here
git commit          # one squashed commit
```

Two things to get right:

1. **Propose it as an addition, not a replacement.** The current diff *renames*
   `hello_world_mpi` → `report_placement` and deletes the old source. 736 lines of new C++
   replacing a file upstream has used everywhere is a big ask in one PR. Keep
   `hello_world_mpi.cxx`, add `report_placement`, switch the smoke steps over, and let him
   retire the old one when he is ready. Reviewable, and revertable by him in one line.
2. **Lead with the motivation, not the feature list.** `hello_world_mpi` proves an image
   *runs*; it cannot distinguish a correct hybrid launch from 128 ranks piled onto core 0.
   `report_placement` reports rank/thread → cpu/core/socket/NUMA/L3 plus the affinity mask,
   so a mis-bound image fails in CI instead of on the cluster. Note the design choice it
   shares with [xthi](https://github.com/ARCHER2-HPC/xthi): `cpu` is a `sched_getcpu()`
   sample and varies between identical runs, so the *mask* is the stable identity — this is
   why both columns exist.

**DECISION B:** addition (recommended) or replacement?

---

## 5. Sequencing against the benchmark runner plan

**§3 was the only prerequisite, and it is done. Neither PR is blocking.**

### §3 is done

It mattered for the benchmark plan, not for the PR: `BenchmarkRunnerPlan.md` §7 records
`compiler` and image identity in every result row, and the in-image `ONEAPI_VERSION` from
`config_env.sh` is where that provenance comes from. While the build-args were half
migrated, oneAPI images claimed one version and ran another, which would have put a wrong
compiler version on every oneAPI row. All four consumers now agree — see the §3 table — so
benchmark provenance is trustworthy.

One consequence to carry forward: the oneAPI images published before 2026-08-18 were built
with the 2026.1.0 *toolkit* while labelled 2025.2.1 or 2026.0.0. **Any results measured
against those images have the wrong compiler version recorded and should be re-run**, not
just relabelled — the binaries themselves came from a different compiler than the row
claims.

### The PRs are not blocking

Every phase of the benchmark plan lives in Bucket B:

| Benchmark phase | Touches | Bucket |
|---|---|---|
| 0 — rule table, topology probe | `libexec/check_placement.sh`, `libexec/fixtures/` | B |
| 1 — provenance, `results.jsonl` | `bench/`, `libexec/` | B |
| 2 — app contract | `scripts/build_hpcg.sh`, `containers/apps/` | B |
| 3 — config + submit, rename | `bench/`, `PBS/` | B |
| 4 — site profile, Casper | `sites/`, `libexec/` | B |
| 5 — app image builder | `containers/apps/`, fork workflows | B |

So the two efforts do not collide, and upstream being 0 ahead means waiting costs nothing
in rebase pain.

### One window that closes

`BenchmarkRunnerPlan.md` §11 Q7 (GPU placement columns, following Benchpark's
`affinity.cuda`/`affinity.rocm` split) would modify `report_placement.cxx` — the one Bucket
A file the benchmark work would ever touch. Phases 0–5 do not touch it.

- **PR 2 before acting on Q7** → upstream gets the CPU-only version, GPU columns follow as
  a small second PR.
- **PR 2 after** → either a much larger first PR, or we carry a permanent local delta on a
  file we have upstreamed.

The first is clearly better, and phases 0–5 give plenty of room. No rush, just don't start
Q7 without sending PR 2.

### Suggested order

1. ~~**§3 oneAPI reconciliation**~~ — **done 2026-08-18.**
2. **Benchmark plan phase 0** — the placement-rule bug in `BenchmarkRunnerPlan.md` §5 makes
   the current sweep report FAIL for every configuration, so nothing measured is
   trustworthy until it is fixed.
3. ~~**PR 1 (oneAPI)**~~ — **submitted 2026-08-18 as benkirk#35.** Open, awaiting review;
   nothing to do but watch it.
4. **PR 2 (`report_placement`)** — any time before benchmark Q7.
5. Benchmark phases 1–5.

---

## 6. Open questions

1. ~~**DECISION A**~~ — **answered: 2026.1.0 everywhere** (§3), with 2025.3.2 kept as a
   selectable fallback in `derecho-images-ghcr.yaml`.
2. **DECISION B** — `report_placement` as an addition or a replacement? Still open, and
   still the one thing PR 2 waits on.
3. **Is the 2026.0.0 HPCG miscompile worth reporting to Intel?** It reproduces at one rank
   under pure OpenMP with a stock HPCG, which is about as small a repro as such a bug gets.
   Nothing in this repo depends on the answer, but the finding is more useful upstream of
   upstream.
4. ~~**Drop the `2025.3.2` fallback from `derecho-images-ghcr.yaml`?**~~ — **decided
   2026-08-18: keep it for now.** It was added to A/B a suspect oneAPI release, and that
   issue is solved, but the dispatch input is a generally useful lever. Clean up later.
5. **Does upstream want the Derecho harness eventually?** `SeparationAnalysis.md` argues
   `make_apptainer_launcher.sh` and the host-MPI/ABI-shim machinery belong in the factory
   *if the factory certifies images for NCAR systems*. That is a real question for benkirk,
   not for us — but it is worth asking before the harness grows a `sites/` abstraction, so
   the answer shapes the design instead of arriving after it.
6. **Is the fork intended to converge or diverge?** If it converges, more of Bucket B
   eventually becomes Bucket A and it is worth keeping fork-only changes on clearly
   separable paths. If it diverges permanently, only correctness fixes flow upstream and
   the rest of this document is a one-time exercise.
7. **`comprehend/workflow-dependency-auto-refresh`** is 8 commits ahead of upstream's copy,
   but the premise has shifted since the first draft: benkirk#34 already **merged** that
   branch on 2026-07-23 — into upstream's *topic branch of the same name*, not `main`, which
   is why `upstream/main` never moved. So those 8 commits are follow-up work on an
   already-merged track. Second PR to that branch, redirect at `main`, or abandon?
