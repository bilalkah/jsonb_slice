#!/usr/bin/env python3
"""Build the landing page and versioned documentation."""

from __future__ import annotations

import argparse
import html
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path


VERSION_PATTERN = re.compile(
    r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)"
    r"(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$"
)


def run(command: list[str], *, cwd: Path, env: dict[str, str] | None = None) -> str:
    completed = subprocess.run(
        command,
        cwd=cwd,
        env=env,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if completed.returncode:
        if completed.stdout:
            print(completed.stdout, file=sys.stderr, end="")
        raise subprocess.CalledProcessError(
            completed.returncode, command, output=completed.stdout
        )
    return completed.stdout


def read_plan(root: Path) -> dict[str, object]:
    return json.loads((root / "release-plan.json").read_text(encoding="utf-8"))


def tags(root: Path) -> dict[str, str]:
    output = run(["git", "tag", "--list", "v*"], cwd=root)
    found: dict[str, str] = {}
    for tag in output.splitlines():
        version = tag.removeprefix("v")
        if VERSION_PATTERN.fullmatch(version):
            found[version] = tag
    return found


def safe_extract(archive: Path, target: Path) -> None:
    target_resolved = target.resolve()
    with tarfile.open(archive) as source:
        for member in source.getmembers():
            member_target = (target / member.name).resolve()
            if target_resolved not in member_target.parents and member_target != target_resolved:
                raise ValueError(f"unsafe archive path: {member.name}")
        source.extractall(target, filter="data")


def export_tag(root: Path, tag: str, target: Path) -> None:
    archive = target.parent / f"{tag}.tar"
    subprocess.run(
        ["git", "archive", "--format=tar", f"--output={archive}", tag],
        cwd=root,
        check=True,
    )
    safe_extract(archive, target)


def build_docs(source: Path, destination: Path, version: str) -> None:
    env = os.environ.copy()
    env["JSONB_SLICE_SITE_URL"] = (
        f"https://bilalkah.github.io/jsonb_slice/{version}/"
    )
    output = run(
        [
            sys.executable,
            "-m",
            "mkdocs",
            "build",
            "--strict",
            "--config-file",
            str(source / "mkdocs.yml"),
            "--site-dir",
            str(destination),
        ],
        cwd=source,
        env=env,
    )
    if output.strip():
        print(output.strip())


def release_row(entry: dict[str, object], available: bool, preview: bool) -> str:
    version = html.escape(str(entry["version"]))
    title = html.escape(str(entry["title"]))
    summary = html.escape(str(entry["summary"]))
    if available:
        label = "Preview" if preview else "Read docs"
        action = f'<a class="release-link" href="./{version}/">{label}</a>'
    else:
        labels = {"ready": "Ready", "released": "Released"}
        state = labels[str(entry["state"])]
        action = f'<span class="release-state">{state}</span>'
    return (
        '<article class="release-row">'
        f'<div class="release-version">{version}</div>'
        '<div class="release-copy">'
        f'<h3>{title}</h3><p>{summary}</p>'
        "</div>"
        f"{action}"
        "</article>"
    )


def build_landing(
    root: Path,
    output: Path,
    plan: dict[str, object],
    built: set[str],
    preview: set[str],
) -> None:
    releases = sorted(plan["releases"], key=lambda item: item["sequence"])
    rows = "\n        ".join(
        release_row(entry, entry["version"] in built, entry["version"] in preview)
        for entry in releases
    )
    ordered_built = [entry["version"] for entry in releases if entry["version"] in built]
    if ordered_built:
        latest = ordered_built[-1]
        latest_action = (
            f'<a class="button" href="./{html.escape(latest)}/">'
            f"Read {html.escape(latest)}</a>"
        )
    else:
        latest_action = '<span class="release-state">First release is ready</span>'

    template = (root / "landing" / "index.html").read_text(encoding="utf-8")
    page = template.replace("@@LATEST_ACTION@@", latest_action).replace(
        "@@VERSION_ROWS@@", rows
    )
    (output / "index.html").write_text(page, encoding="utf-8")
    shutil.copy2(root / "landing" / "404.html", output / "404.html")
    shutil.copy2(root / "landing" / "styles.css", output / "styles.css")
    (output / ".nojekyll").write_text("", encoding="utf-8")


def write_version_index(
    root: Path,
    output: Path,
    plan: dict[str, object],
    built: set[str],
) -> None:
    releases = sorted(plan["releases"], key=lambda item: item["sequence"])
    available = [entry for entry in releases if entry["version"] in built]
    index: list[dict[str, object]] = []
    for position, entry in enumerate(available):
        aliases = ["latest"] if position == len(available) - 1 else []
        index.append(
            {
                "version": entry["version"],
                "title": entry["version"],
                "aliases": aliases,
            }
        )
    (output / "versions.json").write_text(
        json.dumps(index, indent=2) + "\n", encoding="utf-8"
    )
    shutil.copy2(root / "release-plan.json", output / "release-plan.json")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=Path("_site"))
    parser.add_argument(
        "--include-worktree",
        action="store_true",
        help="also build VERSION from the current worktree as a local preview",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    if output == root or not output.is_relative_to(root):
        raise ValueError("--output must be a child directory of the repository")
    plan = read_plan(root)
    known_versions = {entry["version"] for entry in plan["releases"]}
    found_tags = tags(root)
    unknown = sorted(set(found_tags) - known_versions)
    if unknown:
        raise ValueError(f"release tags are missing from release-plan.json: {unknown}")
    missing_tags = [
        entry["version"]
        for entry in plan["releases"]
        if entry["state"] == "released" and entry["version"] not in found_tags
    ]
    if missing_tags:
        raise ValueError(f"released versions have no tag: {missing_tags}")

    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)

    built: set[str] = set()
    preview: set[str] = set()
    with tempfile.TemporaryDirectory(prefix="jsonb-slice-pages-") as temp_name:
        temp = Path(temp_name)
        for version, tag in found_tags.items():
            source = temp / version
            source.mkdir()
            export_tag(root, tag, source)
            build_docs(source, output / version, version)
            built.add(version)

    if args.include_worktree:
        version = (root / "VERSION").read_text(encoding="utf-8").strip()
        if version not in found_tags:
            build_docs(root, output / version, version)
            built.add(version)
            preview.add(version)

    build_landing(root, output, plan, built, preview)
    write_version_index(root, output, plan, built)
    print(f"Pages site built at {output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Pages build failed: {error}", file=sys.stderr)
        raise SystemExit(1)
