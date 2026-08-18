# Plan: What To Send Upstream, And What To Keep In The Fork

Status: **proposal.** Nothing has been branched, cherry-picked, or pushed.

Handoff note for a future session. It records the fork/upstream split decision while it is
fresh, so the next session starts from a decision instead of re-deriving one.

Companion to [BenchmarkRunnerPlan.md](BenchmarkRunnerPlan.md); see §5 for how the two
interact. Short version: **one small fix in §3 should land before benchmark work; neither
PR is a prerequisite.**

---

## 1. Facts

Measured 2026-08-17, after `git fetch upstream`.

| | |
|---|---|
| Fork | `kedziora-csg/demo_github_actions` (`origin`) |
| Upstream | `benkirk/demo_github_actions` (`upstream`), default branch `main` |
| Divergence | **76 commits ahead, 0 behind** `upstream/main` |
| Merge base | `f471750` — "June refresh (#31)", 2026-06-26 |
| Total diff | 41 files, +6975 / −47 |
| PR target | `benkirk:main` — upstream merges PRs there (`#31`) |
| Other upstream branches | `cuda13`, `comprehend/workflow-dependency-auto-refresh` |

Two consequences worth noting:

- **Zero commits behind** means any topic branch cut from `upstream/main` applies without a
  rebase, and there is no upstream drift creating time pressure. Upstream has been idle for
  seven weeks.
- We also hold `comprehend/workflow-dependency-auto-refresh` **8 ahead** of upstream's copy
  of the same branch. That is a separate collaboration track with its own history; it is
  out of scope here and should not be folded into either PR below.

---

## 2. The split

Roughly 1,775 of the 6,975 added lines are plausibly upstream's. The rest is fork
infrastructure and design work.

### Bucket A — send upstream

| Change | Files | Why it is upstream's |
|---|---|---|
| **oneAPI unified toolkit + `ifx` guard** | `containers/devenv/Dockerfile` (oneapi stage) | Fixes a live trap in upstream's tree, not a preference |
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

## 3. Blocker: the oneAPI build-args are half migrated

`35d6f51` and `3d5e2de` renamed the Dockerfile's oneAPI ARGs from
`ONEAPI_CC_URL` + `ONEAPI_FC_URL` to a single `ONEAPI_TOOLKIT_URL`
([Dockerfile:691-692](containers/devenv/Dockerfile#L691-L692)). Only one of the four
consumers was updated.

| Workflow | Passes | Declared? | Actually installs | Stamps into `config_env.sh` |
|---|---|---|---|---|
| `derecho-images-ghcr.yaml:78,83` | `ONEAPI_TOOLKIT_URL` 2025.3.1 | yes | 2025.3.1 | 2025.3.1 ✅ |
| `devel-build-images.yaml:42-44` | `ONEAPI_CC_URL`, `ONEAPI_FC_URL` | **no** | 2026.1.0 (default) | **2025.2.1** ❌ |
| `matrix-build-images.yaml:98-100` | `ONEAPI_CC_URL`, `ONEAPI_FC_URL` | **no** | 2026.1.0 (default) | **2026.0.0** ❌ |
| `matrix-build-images-ghcr.yaml:117-119` | `ONEAPI_CC_URL`, `ONEAPI_FC_URL` | **no** | 2026.1.0 (default) | **2025.2.1** ❌ |

Undeclared build-args are a buildx **warning**, not an error, so three of four oneAPI
consumers silently install the Dockerfile default while recording a different version. This
breaks the invariant `CLAUDE.md` states directly:

> the **real, authoritative versions are the CI matrices**, which override the ARGs via
> build-args

**Fix, before anything else:** in each of the three lagging workflows, replace the two URL
lines with one `ONEAPI_TOOLKIT_URL` and make `ONEAPI_VERSION` agree with it. Decide per
workflow which oneAPI is actually wanted — `devel-build-images.yaml` and
`matrix-build-images.yaml` are upstream-shared files, so whatever lands there is also the
PR content; `matrix-build-images-ghcr.yaml` is fork-only.

Also confirm the intended default. The Dockerfile default is now the merged 2026.1.0
toolkit, while `derecho-images-ghcr.yaml` deliberately pins the 2025.3.1 **HPC** toolkit —
per `CLAUDE.md`, the Derecho site default is `intel/2025.2.1`, so the Derecho pin is
intentional. Is 2026.1.0 the right default for everything else, or should the default track
the Derecho pin?

**DECISION A:** which oneAPI version does each of the three workflows want?

---

## 4. The two PRs

### PR 1 — oneAPI: one toolkit URL, and fail loudly when `ifx` is missing

**Source commits:** `35d6f51`, `3d5e2de`. Both touch only
`containers/devenv/Dockerfile` and `derecho-images-ghcr.yaml`, so a cherry-pick is clean
once the fork-only workflow hunk is dropped.

```bash
git checkout -b upstream/oneapi-single-toolkit upstream/main
git cherry-pick 35d6f51 3d5e2de           # then drop the derecho-images-ghcr.yaml hunks
# add the reconciliation of devel-build-images.yaml + matrix-build-images.yaml (§3)
```

Proposed description, from the comment block already in the Dockerfile:

> The oneAPI offline-installer filenames are a trap. Through 2025.x, three SKUs exist and
> `intel-oneapi-base-toolkit-*` has `icx` but **no `ifx`** — `intel-oneapi-hpc-toolkit-*`
> is the one to use. From 2026.0, Base and HPC were merged, so `intel-oneapi-toolkit-*`
> (no infix) is the merged bundle and *is* correct.
>
> Picking a base-toolkit URL does not fail at install time: `which ifx` comes back empty,
> `FC` silently degrades to gfortran, and the build dies much later linking HDF5. This
> collapses the two URLs into one `ONEAPI_TOOLKIT_URL` and adds an `icx`/`icpx`/`ifx`
> presence check right after `setvars.sh`, turning a confusing late failure into an
> immediate, self-describing one.

Low risk, self-contained, fixes a real failure mode. Send this one first.

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

**Only §3 is a prerequisite. Neither PR is.**

### Do §3 first

Not for the PR's sake — because it corrupts the benchmark results the other plan is built
to produce. `BenchmarkRunnerPlan.md` §7 records `compiler` and image identity in every
result row, and the in-image `ONEAPI_VERSION` from `config_env.sh` is where that provenance
comes from. Benchmarking oneAPI images that claim 2025.2.1 while running 2026.1.0 puts a
wrong compiler version on every oneAPI row in the results table — exactly the kind of
silent mislabelling the results contract exists to prevent. It is a ~20 minute fix.

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

1. **§3 oneAPI reconciliation** — now, before benchmark work. Fork-local, no PR needed.
2. **Benchmark plan phase 0** — the placement-rule bug in `BenchmarkRunnerPlan.md` §5 makes
   the current sweep report FAIL for every configuration, so nothing measured is
   trustworthy until it is fixed.
3. **PR 1 (oneAPI)** — any time after step 1; it is mostly written already.
4. **PR 2 (`report_placement`)** — any time before benchmark Q7.
5. Benchmark phases 1–5.

---

## 6. Open questions

1. **DECISION A** — which oneAPI version for each of the three lagging workflows?
2. **DECISION B** — `report_placement` as an addition or a replacement?
3. **Does upstream want the Derecho harness eventually?** `SeparationAnalysis.md` argues
   `make_apptainer_launcher.sh` and the host-MPI/ABI-shim machinery belong in the factory
   *if the factory certifies images for NCAR systems*. That is a real question for benkirk,
   not for us — but it is worth asking before the harness grows a `sites/` abstraction, so
   the answer shapes the design instead of arriving after it.
4. **Is the fork intended to converge or diverge?** If it converges, more of Bucket B
   eventually becomes Bucket A and it is worth keeping fork-only changes on clearly
   separable paths. If it diverges permanently, only correctness fixes flow upstream and
   the rest of this document is a one-time exercise.
5. **`comprehend/workflow-dependency-auto-refresh`** is 8 commits ahead of upstream's copy.
   Separate PR, separate session, or abandon?
