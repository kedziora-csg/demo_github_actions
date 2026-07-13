# Workflow Summary

This file is manually maintained and is not modified by automation.

## build-hpc-development-image.yaml

- Purpose: Reusable workflow that builds, tests, and optionally publishes a single HPC development image variant.
- Triggers: `workflow_call` from other workflows.
- Key jobs:
  - `build-image`: Build image layers, run image tests, and publish deployment image/tags when enabled.

## devel-build-images.yaml

- Purpose: Development/PR matrix build of selected compiler/OS/MPI combinations, followed by in-container app smoke tests.
- Triggers: `workflow_dispatch`, `pull_request` (Dockerfile and scripts paths).
- Key jobs:
  - `build-matrix`: Calls the reusable build workflow for a reduced validation matrix.
  - `apps-matrix`: Waits on `build-matrix`, then compiles/tests apps inside built images.

## matrix-build-images.yaml

- Purpose: Main production matrix build workflow across compilers, MPI stacks, GPU options, and architectures.
- Triggers: `workflow_dispatch`.
- Key jobs:
  - `build-matrix`: Calls the reusable build workflow for full matrix combinations.

## dial-an-image.yaml

- Purpose: On-demand single-variant builder to reproduce and iterate quickly on one configuration.
- Triggers: `workflow_dispatch`.
- Key jobs:
  - `build-image`: Calls the reusable build workflow using user-selected inputs.

## matrix-smoketest-applications.yaml

- Purpose: Runs app build/smoke tests inside already-published container images across a broad matrix.
- Triggers: `workflow_dispatch`.
- Key jobs:
  - `run-matrix`: Executes hello-world and selected NCAR app build scripts in target images.

## trigger-workflows.yaml

- Purpose: Orchestrator workflow that dispatches matrix builds for selected base operating systems.
- Triggers: `workflow_dispatch`, monthly `schedule`, and closed `pull_request` on `main`.
- Key jobs:
  - `trigger-workflows`: Uses GitHub CLI to dispatch `matrix-build-images.yaml` multiple times.

## container-build.yaml

- Purpose: Validates software builds inside pre-existing container images (hello world, DART, optional Kokkos).
- Triggers: `workflow_dispatch`, `push` to `ci_cd`.
- Key jobs:
  - `build-from-container-env`: Runs compile/test tasks in matrix-selected container runtimes.

## derived-containers.yaml

- Purpose: Builds downstream/demo container images after container build workflow completion.
- Triggers: `workflow_dispatch`, `workflow_run` on completion of workflow named `Container Build`.
- Key jobs:
  - `build-my-containers`: Builds `containers/demo` image variants.

## conda-build.yaml

- Purpose: Verifies compiler toolchain and hello-world builds from the conda environment definition.
- Triggers: `workflow_dispatch`, `push` to `ci_cd`, weekly `schedule`.
- Key jobs:
  - `build-from-conda-env`: Sets up conda env from `conda.yaml` and compiles sample programs.

## manually-clean-action-log.yaml

- Purpose: Manual cleanup of old GitHub Actions logs.
- Triggers: `workflow_dispatch` with retention input.
- Key jobs:
  - `delete-old-actions`: Invokes `yanovation/delete-old-actions` action.

## cron-clean-action-log.yaml

- Purpose: Scheduled cleanup of old GitHub Actions logs.
- Triggers: `workflow_dispatch`, monthly `schedule`.
- Key jobs:
  - `delete-old-actions`: Invokes `yanovation/delete-old-actions` action.

## mega-linter.yml

- Purpose: Repository linting/quality checks.
- Triggers: Defined in the workflow file.
- Key jobs:
  - Review this workflow directly for current lint tool scope and matrix details.
