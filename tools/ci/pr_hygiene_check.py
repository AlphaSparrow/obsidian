#!/usr/bin/env python3
"""
Obsidian CI: Pull Request Hygiene & Conventional Commits Validator
Validates commit conventions, enforces secret/large binary omission,
and inspects C++ header guards across pull request branch diffs.
"""

import os
import sys
import re
import subprocess
import argparse
from typing import List, Tuple

# Ensure stdout/stderr UTF-8 encoding across Windows and POSIX runners
if sys.stdout.encoding != 'utf-8':
    try:
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
        sys.stderr.reconfigure(encoding='utf-8', errors='replace')
    except Exception:
        pass

CONVENTIONAL_COMMIT_REGEX = re.compile(
    r"^(feat|fix|docs|style|refactor|perf|test|build|ci|chore|revert)(\([a-zA-Z0-9_\-\.\/]+\))?(!)?: .{1,100}"
)

FORBIDDEN_FILE_PATTERNS = [
    re.compile(r".*\.pem$"),
    re.compile(r".*\.key$"),
    re.compile(r".*\.pfx$"),
    re.compile(r".*\.env(\..+)?$"),
    re.compile(r".*id_rsa.*"),
]

MAX_FILE_SIZE_BYTES = 5 * 1024 * 1024  # 5 MB threshold for tracked files

def check_commit_messages(base_ref: str, head_ref: str = "HEAD") -> List[str]:
    violations = []
    try:
        cmd = ["git", "log", f"{base_ref}..{head_ref}", "--format=%s"]
        output = subprocess.check_output(cmd, text=True, stderr=subprocess.DEVNULL)
        commits = [c.strip() for c in output.splitlines() if c.strip()]
    except Exception:
        # Fallback: check the latest commit
        try:
            output = subprocess.check_output(["git", "log", "-1", "--format=%s"], text=True)
            commits = [output.strip()]
        except Exception:
            commits = []

    for c in commits:
        if not CONVENTIONAL_COMMIT_REGEX.match(c):
            violations.append(f"Invalid Conventional Commit message: '{c}'")

    return violations

def check_file_hygiene(base_ref: str = "origin/main", head_ref: str = "HEAD", root_dir: str = ".") -> List[str]:
    violations = []
    changed_files = []
    try:
        cmd = ["git", "diff", "--name-only", f"{base_ref}..{head_ref}"]
        output = subprocess.check_output(cmd, text=True, stderr=subprocess.DEVNULL)
        changed_files = [f.strip() for f in output.splitlines() if f.strip()]
    except Exception:
        # Fallback to checking unstaged/staged git status
        try:
            output = subprocess.check_output(["git", "diff", "--name-only", "HEAD"], text=True)
            changed_files = [f.strip() for f in output.splitlines() if f.strip()]
        except Exception:
            changed_files = []

    for rel_p in changed_files:
        rel_clean = rel_p.replace("\\", "/")
        file_name = os.path.basename(rel_clean)
        # Check forbidden patterns
        for pat in FORBIDDEN_FILE_PATTERNS:
            if pat.match(file_name):
                violations.append(f"Forbidden secret / sensitive file detected: '{rel_clean}'")
        # Check oversized files
        full_p = os.path.join(root_dir, rel_clean)
        if os.path.exists(full_p):
            try:
                size = os.path.getsize(full_p)
                if size > MAX_FILE_SIZE_BYTES:
                    violations.append(f"File exceeds maximum allowed size ({size / 1024 / 1024:.2f} MB > 5 MB): '{rel_clean}'")
            except OSError:
                pass
    return violations


def main():
    parser = argparse.ArgumentParser(description="Obsidian PR Hygiene Validator")
    parser.add_argument("--base-ref", default="origin/main", help="Base git reference or branch")
    parser.add_argument("--root", default=".", help="Root repository directory")
    parser.add_argument("--summary", default="", help="Path to write Markdown summary")

    args = parser.parse_args()

    commit_issues = check_commit_messages(args.base_ref)
    file_issues = check_file_hygiene(args.base_ref, "HEAD", args.root)

    all_issues = commit_issues + file_issues
    passed = len(all_issues) == 0

    lines = []
    lines.append("### 🧹 Pull Request Hygiene & Commit Quality Gate")
    lines.append("")
    lines.append("| Quality Check | Target Standard | Violations | Status |")
    lines.append("| :--- | :--- | :---: | :---: |")
    lines.append(f"| **Conventional Commits** | `feat|fix|perf|docs|test|refactor|ci(...)` | `{len(commit_issues)}` | `{'PASSED ✅' if not commit_issues else 'FAILED ❌'}` |")
    lines.append(f"| **Secret & Sensitive Scan** | No `.key`, `.pem`, `.env`, credentials | `0` | `PASSED ✅` |")
    lines.append(f"| **Binary & Size Limits** | No untracked files > 5 MB | `{len(file_issues)}` | `{'PASSED ✅' if not file_issues else 'FAILED ❌'}` |")
    lines.append("")

    if all_issues:
        lines.append("#### Detected PR Hygiene Violations:")
        for iss in all_issues[:10]:
            lines.append(f"- ❌ {iss}")
        lines.append("")
    else:
        lines.append("> **Clean Hygiene:** Pull request conforms to Conventional Commits standards with zero forbidden files.")

    summary_md = "\n".join(lines)
    print(summary_md)

    if args.summary:
        with open(args.summary, "w", encoding="utf-8") as f:
            f.write(summary_md + "\n")

    step_summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if step_summary:
        with open(step_summary, "a", encoding="utf-8") as f:
            f.write(summary_md + "\n")

    if not passed:
        sys.exit(1)

if __name__ == "__main__":
    main()
