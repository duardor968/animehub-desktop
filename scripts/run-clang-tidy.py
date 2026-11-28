#!/usr/bin/env python3
"""Run clang-tidy against a deduplicated compile_commands.json.

On main or manual triggers (or --full, or when global C++ build/lint config
changes), analyzes every C++ source under src/ and tests/. On dev pushes and
pull requests, analyzes only changed .cpp files and .cpp files that include
changed headers (transitively across project headers).
"""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
import threading

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)
FULL_SWEEP_TRIGGERS = {
    ".clang-tidy",
}
ZERO_SHA = "0" * 40


def git(repo: Path, *args: str, check: bool = False) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", "-c", "safe.directory=*", *args],
        cwd=repo,
        text=True,
        capture_output=True,
        check=check,
    )


def commit_exists(repo: Path, rev: str) -> bool:
    return git(repo, "cat-file", "-e", f"{rev}^{{commit}}").returncode == 0


def ensure_history(repo: Path, ref: str | None = None) -> None:
    is_shallow = git(repo, "rev-parse", "--is-shallow-repository").stdout.strip() == "true"
    if ref:
        git(repo, "fetch", "--no-tags", "origin", f"+refs/heads/{ref}:refs/remotes/origin/{ref}")
    if is_shallow:
        git(repo, "fetch", "--no-tags", "--unshallow", "origin")


def resolve_diff_base(repo: Path, base_override: str | None) -> tuple[str | None, str]:
    if base_override:
        if commit_exists(repo, base_override):
            mb = git(repo, "merge-base", base_override, "HEAD")
            if mb.returncode == 0 and mb.stdout.strip():
                return mb.stdout.strip(), f"merge-base({base_override}, HEAD)"
            return base_override, base_override
        return None, f"override {base_override!r} not found"

    event = os.environ.get("CI_PIPELINE_EVENT", "").strip()
    branch = os.environ.get("CI_COMMIT_BRANCH", "").strip()
    head = git(repo, "rev-parse", "HEAD").stdout.strip()

    if event == "pull_request":
        target = (
            os.environ.get("CI_COMMIT_TARGET_BRANCH", "").strip()
            or branch
            or "main"
        )
        remote_ref = f"origin/{target}"
        if not commit_exists(repo, remote_ref):
            ensure_history(repo, target)
        mb = git(repo, "merge-base", remote_ref, "HEAD")
        if mb.returncode != 0:
            ensure_history(repo, target)
            mb = git(repo, "merge-base", remote_ref, "HEAD")
        if mb.returncode == 0 and mb.stdout.strip():
            return mb.stdout.strip(), f"PR merge-base against {remote_ref}"
        return None, f"could not resolve PR merge-base against {remote_ref}"

    prev = (
        os.environ.get("CI_PREV_COMMIT_SHA", "").strip()
        or os.environ.get("CI_COMMIT_BEFORE_SHA", "").strip()
    )
    if prev and prev != ZERO_SHA and prev != head:
        if not commit_exists(repo, prev):
            ensure_history(repo, branch or None)
        if commit_exists(repo, prev):
            anc = git(repo, "merge-base", "--is-ancestor", prev, "HEAD")
            if anc.returncode == 0:
                return prev, f"previous commit {prev[:7]}"

    if not commit_exists(repo, "origin/main"):
        ensure_history(repo, "main")
    mb = git(repo, "merge-base", "origin/main", "HEAD")
    if mb.returncode != 0:
        ensure_history(repo, "main")
        mb = git(repo, "merge-base", "origin/main", "HEAD")
    if mb.returncode == 0 and mb.stdout.strip() and mb.stdout.strip() != head:
        return mb.stdout.strip(), "merge-base against origin/main"

    if commit_exists(repo, "HEAD~1"):
        parent = git(repo, "rev-parse", "HEAD~1").stdout.strip()
        return parent, f"parent commit {parent[:7]}"
    return None, "no comparable base commit found"


def strip_ccache(command: str) -> str:
    parts = shlex.split(command)
    if parts and os.path.basename(parts[0]) == "ccache":
        parts = parts[1:]
    return shlex.join(parts)


def is_test_entry(entry: dict) -> bool:
    output = str(entry.get("output", "")).replace("\\", "/")
    cmd = str(entry.get("command", "")).replace("\\", "/")
    return output.startswith("tests/") or "/tests/CMakeFiles/" in cmd


def load_deduplicated_db(
    repo: Path, compile_commands_path: Path
) -> tuple[list[dict], list[Path]]:
    raw_db = json.loads(compile_commands_path.read_text(encoding="utf-8"))
    allowed = (str(repo / "src") + os.sep, str(repo / "tests") + os.sep)

    # Keep one canonical compile command per source file, preferring non-test
    # targets when an app source file is compiled into both app and test targets.
    by_real: dict[str, dict] = {}
    for entry in raw_db:
        file_path = entry["file"]
        if not os.path.isabs(file_path):
            file_path = os.path.join(entry["directory"], file_path)
        real = os.path.realpath(file_path)
        cleaned = dict(entry)
        cleaned["file"] = real
        if "command" in cleaned:
            cleaned["command"] = strip_ccache(cleaned["command"])
        elif "arguments" in cleaned and cleaned["arguments"]:
            args = list(cleaned["arguments"])
            if os.path.basename(args[0]) == "ccache":
                args = args[1:]
            cleaned["arguments"] = args

        existing = by_real.get(real)
        if existing is None or (is_test_entry(existing) and not is_test_entry(cleaned)):
            by_real[real] = cleaned

    project_cpp = sorted(
        Path(real)
        for real in by_real
        if real.startswith(allowed) and real.endswith(".cpp") and os.path.exists(real)
    )
    return list(by_real.values()), project_cpp


def find_header_dependents(repo: Path, changed_headers: set[str], all_cpp: list[Path]) -> set[Path]:
    if not changed_headers:
        return set()

    # Build lightweight include graph across project headers + cpp files.
    project_headers: list[Path] = []
    for root_name in ("src", "tests"):
        root = repo / root_name
        if root.exists():
            project_headers.extend(root.rglob("*.h"))

    def includes_of(path: Path) -> list[str]:
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            return []
        return INCLUDE_RE.findall(text)

    def matches_any(inc: str, targets: set[str]) -> bool:
        norm = inc.replace("\\", "/")
        base = norm.rsplit("/", 1)[-1]
        for target in targets:
            if target == norm or target.endswith("/" + norm) or target.rsplit("/", 1)[-1] == base:
                return True
        return False

    affected_headers = set(changed_headers)
    header_includes = {
        h.relative_to(repo).as_posix(): includes_of(h) for h in project_headers
    }
    changed = True
    while changed:
        changed = False
        for rel, incs in header_includes.items():
            if rel in affected_headers:
                continue
            if any(matches_any(inc, affected_headers) for inc in incs):
                affected_headers.add(rel)
                changed = True

    selected: set[Path] = set()
    for cpp in all_cpp:
        if any(matches_any(inc, affected_headers) for inc in includes_of(cpp)):
            selected.add(cpp)
    return selected


def select_files(
    repo: Path, all_cpp: list[Path], force_full: bool, base_override: str | None
) -> tuple[str, list[Path]]:
    event = os.environ.get("CI_PIPELINE_EVENT", "").strip()
    branch = os.environ.get("CI_COMMIT_BRANCH", "").strip()

    if force_full:
        return "full (--full)", all_cpp
    if event == "manual":
        return "full (manual trigger)", all_cpp
    if event == "push" and branch == "main":
        return "full (push to main)", all_cpp

    base, reason = resolve_diff_base(repo, base_override)
    if not base:
        return f"full (fallback: {reason})", all_cpp

    diff = git(repo, "diff", "--name-only", "--diff-filter=ACMR", base, "HEAD")
    if diff.returncode != 0:
        return f"full (fallback: git diff failed: {diff.stderr.strip()})", all_cpp

    changed_rel = [line.strip().replace("\\", "/") for line in diff.stdout.splitlines() if line.strip()]
    config_hit = sorted(FULL_SWEEP_TRIGGERS.intersection(changed_rel))
    if config_hit:
        return f"full (config changed: {', '.join(config_hit)})", all_cpp

    cpp_by_rel = {p.relative_to(repo).as_posix(): p for p in all_cpp}
    selected: set[Path] = set()
    changed_headers: set[str] = set()

    for rel in changed_rel:
        if rel in cpp_by_rel:
            selected.add(cpp_by_rel[rel])
        elif rel.endswith(".h") and (rel.startswith("src/") or rel.startswith("tests/")):
            changed_headers.add(rel)

    if changed_headers:
        selected.update(find_header_dependents(repo, changed_headers, all_cpp))

    return f"incremental ({reason})", sorted(selected)


def main() -> int:
    parser = argparse.ArgumentParser(description="Run clang-tidy incrementally or in full.")
    parser.add_argument("--build-dir", required=True, help="CMake build directory with compile_commands.json")
    parser.add_argument("--clang-tidy", default="clang-tidy", help="clang-tidy executable")
    parser.add_argument("--jobs", type=int, default=4, help="Parallel worker count")
    parser.add_argument("--extra-arg", action="append", default=[], help="Extra argument forwarded to clang-tidy")
    parser.add_argument("--base", default=None, help="Override git base ref for incremental diff")
    parser.add_argument("--full", action="store_true", help="Force full sweep of all project .cpp files")
    parser.add_argument("--dry-run", action="store_true", help="Print selected files and exit without running clang-tidy")
    args = parser.parse_args()

    repo = Path(os.path.realpath(os.getcwd()))
    cc_path = Path(args.build_dir) / "compile_commands.json"
    if not cc_path.is_file():
        print(f"error: compile_commands.json not found at {cc_path}", file=sys.stderr)
        return 1

    dedup_db, all_cpp = load_deduplicated_db(repo, cc_path)
    mode, targets = select_files(repo, all_cpp, args.full, args.base)
    print(f"clang-tidy mode: {mode} — {len(targets)}/{len(all_cpp)} C++ files", flush=True)
    for target in targets:
        print(f"  - {target.relative_to(repo).as_posix()}", flush=True)

    if not targets:
        print("clang-tidy: no C++ files affected; skipping.", flush=True)
        return 0
    if args.dry_run:
        return 0

    with tempfile.TemporaryDirectory(prefix="tidy-db-") as tmp_dir:
        db_out = Path(tmp_dir) / "compile_commands.json"
        db_out.write_text(json.dumps(dedup_db), encoding="utf-8")

        base_cmd = [args.clang_tidy, "-p", tmp_dir, "--quiet"]
        for extra in args.extra_arg:
            base_cmd.append(f"--extra-arg={extra}")

        print_lock = threading.Lock()
        failures: list[tuple[Path, int]] = []

        def run_one(path: Path) -> tuple[Path, int]:
            proc = subprocess.run(
                [*base_cmd, str(path)],
                cwd=repo,
                text=True,
                capture_output=True,
            )
            rel = path.relative_to(repo).as_posix()
            with print_lock:
                print(f"[clang-tidy] {rel} (exit {proc.returncode})", flush=True)
                if proc.stdout:
                    sys.stdout.write(proc.stdout)
                if proc.stderr:
                    sys.stderr.write(proc.stderr)
                sys.stdout.flush()
                sys.stderr.flush()
            return path, proc.returncode

        workers = max(1, min(args.jobs, len(targets)))
        with ThreadPoolExecutor(max_workers=workers) as pool:
            futures = [pool.submit(run_one, path) for path in targets]
            for fut in as_completed(futures):
                path, code = fut.result()
                if code != 0:
                    failures.append((path, code))

    if failures:
        print(f"clang-tidy failed on {len(failures)} file(s):", file=sys.stderr)
        for path, code in sorted(failures):
            print(f"  - {path.relative_to(repo).as_posix()} (exit {code})", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
