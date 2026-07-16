# GitHub Actions Dependency Tree

Generated: 2026-07-13 19:49 UTC

This document is manually maintained.

## Overview

```mermaid
flowchart TD
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
```

## File-Level Dependencies

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

## Workflows With No Explicit Cross-Workflow Links

- conda-build.yaml
- cron-clean-action-log.yaml
- manually-clean-action-log.yaml
- matrix-smoketest-applications.yaml
- mega-linter.yml
