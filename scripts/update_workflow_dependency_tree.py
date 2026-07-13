#!/usr/bin/env python3

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
import re
from typing import Dict, List, Set, Tuple

import yaml


REUSABLE_PREFIXES = ("./.github/workflows/", ".github/workflows/")
WORKFLOW_RUN_RE = re.compile(r"gh\s+workflow\s+run\s+([^\s]+)")


class ActionsLoader(yaml.SafeLoader):
    """YAML loader that does not treat 'on'/'off' as booleans."""


for key, resolvers in list(ActionsLoader.yaml_implicit_resolvers.items()):
    ActionsLoader.yaml_implicit_resolvers[key] = [
        (tag, regex)
        for tag, regex in resolvers
        if tag != "tag:yaml.org,2002:bool"
    ]


@dataclass(frozen=True)
class Edge:
    src_file: str
    src_job: str
    dst_file: str
    dst_job: str
    kind: str


def normalize_needs(needs: object) -> List[str]:
    if isinstance(needs, str):
        return [needs]
    if isinstance(needs, list):
        return [n for n in needs if isinstance(n, str)]
    return []


def node_id(file_name: str, job_name: str) -> str:
    safe = f"{file_name}_{job_name}"
    safe = re.sub(r"[^A-Za-z0-9_]", "_", safe)
    return f"n_{safe}"


def load_workflow(path: Path) -> dict:
    data = yaml.load(path.read_text(encoding="utf-8"), Loader=ActionsLoader)
    return data if isinstance(data, dict) else {}


def main() -> None:
    repo_root = Path(__file__).resolve().parents[1]
    workflows_dir = repo_root / ".github" / "workflows"
    out_path = workflows_dir / "DEPENDENCY_TREE.md"

    workflow_paths = sorted(workflows_dir.glob("*.yaml")) + sorted(workflows_dir.glob("*.yml"))
    workflow_paths = sorted(set(workflow_paths))

    raw: Dict[str, dict] = {}
    workflow_name_to_file: Dict[str, str] = {}
    jobs_by_file: Dict[str, List[str]] = {}

    for wf_path in workflow_paths:
        data = load_workflow(wf_path)
        file_name = wf_path.name
        raw[file_name] = data

        wf_name = data.get("name")
        if isinstance(wf_name, str):
            workflow_name_to_file[wf_name] = file_name

        jobs = data.get("jobs")
        if isinstance(jobs, dict):
            jobs_by_file[file_name] = sorted([k for k in jobs.keys() if isinstance(k, str)])
        else:
            jobs_by_file[file_name] = []

    edges: Set[Edge] = set()
    unresolved_workflow_run: List[Tuple[str, str]] = []

    for file_name in sorted(raw.keys()):
        data = raw[file_name]
        jobs = data.get("jobs") if isinstance(data.get("jobs"), dict) else {}

        for job_name, job_data in sorted(jobs.items()):
            if not isinstance(job_data, dict):
                continue

            # Intra-workflow job dependencies.
            for need in normalize_needs(job_data.get("needs")):
                if need in jobs:
                    edges.add(Edge(file_name, need, file_name, job_name, "needs"))

            # Reusable workflow dependencies.
            uses = job_data.get("uses")
            if isinstance(uses, str) and uses.startswith(REUSABLE_PREFIXES):
                target_file = Path(uses).name
                if target_file in jobs_by_file:
                    for target_job in jobs_by_file[target_file]:
                        edges.add(
                            Edge(file_name, job_name, target_file, target_job, "uses reusable workflow")
                        )

            # Dispatch dependencies via `gh workflow run ...` shell commands.
            steps = job_data.get("steps")
            if isinstance(steps, list):
                for step in steps:
                    if not isinstance(step, dict):
                        continue
                    run_block = step.get("run")
                    if not isinstance(run_block, str):
                        continue

                    for match in WORKFLOW_RUN_RE.finditer(run_block):
                        target = match.group(1).strip().strip("'\"")
                        target_file = None

                        if target in jobs_by_file:
                            target_file = target
                        elif target in workflow_name_to_file:
                            target_file = workflow_name_to_file[target]

                        if target_file:
                            for target_job in jobs_by_file[target_file]:
                                edges.add(
                                    Edge(file_name, job_name, target_file, target_job, "dispatches")
                                )

        # Event dependencies via `on.workflow_run.workflows`.
        on_config = data.get("on")
        if not isinstance(on_config, dict):
            continue

        workflow_run = on_config.get("workflow_run")
        if not isinstance(workflow_run, dict):
            continue

        trigger_workflows = workflow_run.get("workflows")
        if not isinstance(trigger_workflows, list):
            continue

        for workflow_name in trigger_workflows:
            if not isinstance(workflow_name, str):
                continue
            source_file = workflow_name_to_file.get(workflow_name)
            if not source_file:
                unresolved_workflow_run.append((file_name, workflow_name))
                continue

            for source_job in jobs_by_file.get(source_file, []):
                for target_job in jobs_by_file.get(file_name, []):
                    edges.add(
                        Edge(source_file, source_job, file_name, target_job, "workflow_run")
                    )

    all_nodes: List[Tuple[str, str]] = []
    for file_name in sorted(jobs_by_file.keys()):
        for job_name in jobs_by_file[file_name]:
            all_nodes.append((file_name, job_name))

    edge_sort_key = lambda e: (e.src_file, e.src_job, e.dst_file, e.dst_job, e.kind)
    sorted_edges = sorted(edges, key=edge_sort_key)

    mermaid_lines = ["```mermaid", "flowchart TD"]
    for file_name, job_name in all_nodes:
        label = f"{file_name} :: {job_name}"
        mermaid_lines.append(f"  {node_id(file_name, job_name)}[{label}]")

    if sorted_edges:
        mermaid_lines.append("")
    for edge in sorted_edges:
        mermaid_lines.append(
            f"  {node_id(edge.src_file, edge.src_job)} -->|{edge.kind}| {node_id(edge.dst_file, edge.dst_job)}"
        )
    mermaid_lines.append("```")

    file_level_edges: Set[Tuple[str, str, str]] = set()
    for edge in sorted_edges:
        if edge.src_file != edge.dst_file:
            file_level_edges.add((edge.src_file, edge.dst_file, edge.kind))

    involved_cross_file: Set[str] = set()
    for src_file, dst_file, _ in file_level_edges:
        involved_cross_file.add(src_file)
        involved_cross_file.add(dst_file)

    standalone_files = [
        wf for wf in sorted(jobs_by_file.keys()) if wf not in involved_cross_file
    ]

    now = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")

    lines: List[str] = []
    lines.append("# GitHub Actions Dependency Tree")
    lines.append("")
    lines.append(f"Generated: {now}")
    lines.append("")
    lines.append("This document is auto-generated by `scripts/update_workflow_dependency_tree.py`.")
    lines.append("")
    lines.append("## Overview")
    lines.append("")
    lines.extend(mermaid_lines)
    lines.append("")
    lines.append("## File-Level Dependencies")
    lines.append("")

    if file_level_edges:
        for src_file, dst_file, kind in sorted(file_level_edges):
            lines.append(f"- {src_file} -> {dst_file} ({kind})")
    else:
        lines.append("- None")

    lines.append("")
    lines.append("## Job-Level Dependencies")
    lines.append("")

    if sorted_edges:
        for edge in sorted_edges:
            lines.append(
                f"- {edge.src_file}::{edge.src_job} -> {edge.dst_file}::{edge.dst_job} ({edge.kind})"
            )
    else:
        lines.append("- None")

    lines.append("")
    lines.append("## Workflows With No Explicit Cross-Workflow Links")
    lines.append("")
    if standalone_files:
        for wf in standalone_files:
            lines.append(f"- {wf}")
    else:
        lines.append("- None")

    if unresolved_workflow_run:
        lines.append("")
        lines.append("## Unresolved workflow_run References")
        lines.append("")
        for target_file, workflow_name in sorted(unresolved_workflow_run):
            lines.append(
                f"- {target_file} references workflow name '{workflow_name}' that was not found"
            )

    rendered = "\n".join(lines) + "\n"

    if out_path.exists() and out_path.read_text(encoding="utf-8") == rendered:
        print("No changes to DEPENDENCY_TREE.md")
        return

    out_path.write_text(rendered, encoding="utf-8")
    print("Updated .github/workflows/DEPENDENCY_TREE.md")


if __name__ == "__main__":
    main()
