# GitHub Actions Dependency Tree

<<<<<<< HEAD
Generated: 2026-07-13 19:49 UTC

This document is manually maintained.
=======
Generated: 2026-07-13

This document maps dependencies between workflow files and jobs in `.github/workflows`.
>>>>>>> cd4331fba007d98c674e56c76c27fbb3a90878aa

## Overview

```mermaid
flowchart TD
<<<<<<< HEAD
  n_conda_build_yaml_build_from_conda_env[conda-build.yaml :: build-from-conda-env]
  n_container_build_yaml_build_from_container_env[container-build.yaml :: build-from-container-env]
  n_derived_containers_yaml_build_my_containers[derived-containers.yaml :: build-my-containers]
  n_devel_build_images_yaml_apps_matrix[devel-build-images.yaml :: apps-matrix]
  n_devel_build_images_yaml_build_matrix[devel-build-images.yaml :: build-matrix]
  n_dial_an_image_yaml_build_image[dial-an-image.yaml :: build-image]
  n_matrix_build_images_yaml_build_matrix[matrix-build-images.yaml :: build-matrix]
  n_trigger_workflows_yaml_trigger_workflows[trigger-workflows.yaml :: trigger-workflows]

  n_container_build_yaml_build_from_container_env -->|workflow_run| n_derived_containers_yaml_build_my_containers
  n_devel_build_images_yaml_build_matrix -->|uses reusable workflow| n_build_hpc_development_image_yaml_build_image
  n_devel_build_images_yaml_build_matrix -->|needs| n_devel_build_images_yaml_apps_matrix
  n_dial_an_image_yaml_build_image -->|uses reusable workflow| n_build_hpc_development_image_yaml_build_image
  n_matrix_build_images_yaml_build_matrix -->|uses reusable workflow| n_build_hpc_development_image_yaml_build_image
  n_trigger_workflows_yaml_trigger_workflows -->|dispatches| n_matrix_build_images_yaml_build_matrix
=======
  devel_build_matrix[devel-build-images.yaml :: build-matrix]
  devel_apps_matrix[devel-build-images.yaml :: apps-matrix]
  matrix_build_matrix[matrix-build-images.yaml :: build-matrix]
  dial_build_image[dial-an-image.yaml :: build-image]
  reusable_build_image[build-hpc-development-image.yaml :: build-image]

  container_build[container-build.yaml :: build-from-container-env]
  derived_build[derived-containers.yaml :: build-my-containers]

  trigger_job[trigger-workflows.yaml :: trigger-workflows]

  devel_build_matrix -->|uses reusable workflow| reusable_build_image
  matrix_build_matrix -->|uses reusable workflow| reusable_build_image
  dial_build_image -->|uses reusable workflow| reusable_build_image

  devel_build_matrix -->|needs| devel_apps_matrix

  container_build -->|workflow_run on completion| derived_build

  trigger_job -->|gh workflow run matrix-build-images.yaml| matrix_build_matrix
>>>>>>> cd4331fba007d98c674e56c76c27fbb3a90878aa
```

## File-Level Dependencies

<<<<<<< HEAD
- container-build.yaml -> derived-containers.yaml (workflow_run)
- devel-build-images.yaml -> build-hpc-development-image.yaml (uses reusable workflow)
- dial-an-image.yaml -> build-hpc-development-image.yaml (uses reusable workflow)
- matrix-build-images.yaml -> build-hpc-development-image.yaml (uses reusable workflow)
- trigger-workflows.yaml -> matrix-build-images.yaml (dispatches)

## Job-Level Dependencies

- container-build.yaml::build-from-container-env -> derived-containers.yaml::build-my-containers (workflow_run)
- devel-build-images.yaml::build-matrix -> build-hpc-development-image.yaml::build-image (uses reusable workflow)
- devel-build-images.yaml::build-matrix -> devel-build-images.yaml::apps-matrix (needs)
- dial-an-image.yaml::build-image -> build-hpc-development-image.yaml::build-image (uses reusable workflow)
- matrix-build-images.yaml::build-matrix -> build-hpc-development-image.yaml::build-image (uses reusable workflow)
- trigger-workflows.yaml::trigger-workflows -> matrix-build-images.yaml::build-matrix (dispatches)
=======
- devel-build-images.yaml -> build-hpc-development-image.yaml
- matrix-build-images.yaml -> build-hpc-development-image.yaml
- dial-an-image.yaml -> build-hpc-development-image.yaml
- container-build.yaml -> derived-containers.yaml (via `workflow_run`)
- trigger-workflows.yaml -> matrix-build-images.yaml (via `gh workflow run` shell command)

## Job-Level Dependencies

- devel-build-images.yaml
  - build-matrix (calls reusable workflow)
  - apps-matrix (needs: build-matrix)

- matrix-build-images.yaml
  - build-matrix (calls reusable workflow)

- dial-an-image.yaml
  - build-image (calls reusable workflow)

- build-hpc-development-image.yaml
  - build-image (reusable workflow entrypoint job)

- container-build.yaml
  - build-from-container-env

- derived-containers.yaml
  - build-my-containers (triggered by completed runs of workflow named "Container Build")

- trigger-workflows.yaml
  - trigger-workflows (dispatches matrix-build-images.yaml)
>>>>>>> cd4331fba007d98c674e56c76c27fbb3a90878aa

## Workflows With No Explicit Cross-Workflow Links

- conda-build.yaml
<<<<<<< HEAD
- cron-clean-action-log.yaml
- manually-clean-action-log.yaml
- matrix-smoketest-applications.yaml
- mega-linter.yml
=======
- matrix-smoketest-applications.yaml
- manually-clean-action-log.yaml
- cron-clean-action-log.yaml
>>>>>>> cd4331fba007d98c674e56c76c27fbb3a90878aa
