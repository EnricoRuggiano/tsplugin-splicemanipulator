"""Cross-platform functional tests for the TSDuck SCTE-35 plugin.

Each JSON file in tests/cases describes one tsp execution:

{
  "name":        "human readable name",
    "plugin":      "splicemanipulator",       # optional, defaults to $PLUGIN or "splicemanipulator"
  "plugin_args": ["--splice-pid", "96"],    # arguments passed to -P <plugin>
  "input":       "data/synthetic.ts",       # optional, relative to tests/
  "inject":      "data/tests.bin",          # optional, relative to tests/, "" disables
  "splice_pid":  96,                        # PID used by pmt/inject/splicemonitor
  "monitor":     true,                      # optional, add -P splicemonitor
  "timeout":     60,                        # optional, seconds
  "expect": {
      "exit_code": 0,                       # int or list of accepted codes
      "contains":     ["text", ...],
      "not_contains": ["text", ...],
      "matches":      ["regex", ...],
      "not_matches":  ["regex", ...],
      "counts":       {"regex": 2, ...}
  }
}

Paths inside a case are relative to the tests/ directory, so the same case
works on Windows and Linux.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
from pathlib import Path

import pytest

TESTS_DIR = Path(__file__).resolve().parent
ROOT_DIR = TESTS_DIR.parent
CASES_DIR = TESTS_DIR / "cases"

DEFAULT_PLUGIN = os.environ.get("PLUGIN", "splicemanipulator")
DEFAULT_TIMEOUT = 60


def plugin_dirs() -> list[Path]:
    """Directories where the freshly built plugin may be located."""
    env_dir = os.environ.get("PLUGIN_DIR")
    if env_dir:
        return [Path(env_dir)]
    build = ROOT_DIR / "build"
    if os.name == "nt":
        return [build / "Release", build / "Debug", build]
    return [build]


def build_env() -> dict[str, str]:
    env = dict(os.environ)
    dirs = [str(d) for d in plugin_dirs() if d.is_dir()]
    previous = env.get("TSPLUGINS_PATH", "")
    if previous:
        dirs.append(previous)
    env["TSPLUGINS_PATH"] = os.pathsep.join(dirs)
    return env


def resolve(relative: str) -> str:
    """Resolve a case-relative path into an absolute, OS-native path."""
    return str((TESTS_DIR / Path(relative)).resolve())


def expand_plugin_args(args: list[str]) -> list[str]:
    """Turn case-relative file arguments into absolute paths."""
    file_options = {"--rules", "-r"}
    result: list[str] = []
    take_path = False
    for arg in args:
        if take_path:
            result.append(resolve(arg))
            take_path = False
        elif arg in file_options:
            result.append(arg)
            take_path = True
        else:
            result.append(str(arg))
    return result


def build_command(case: dict) -> list[str]:
    plugin = case.get("plugin", DEFAULT_PLUGIN)
    splice_pid = str(case.get("splice_pid", 96))
    input_file = case.get("input", "data/synthetic.ts")
    inject_file = case.get("inject", "data/tests.bin")

    cmd = ["tsp", "-v", "--add-input-stuffing", "1/5",
           "-I", "file", resolve(input_file),
           "-P", "pmt", "--service", "1",
           "--add-programinfo-id", "0x43554549",
           "--add-pid", f"{splice_pid}/0x86"]

    if inject_file:
        cmd += ["-P", "inject", "--pid", splice_pid,
                "--inter-packet", "1", "--repeat", "1", resolve(inject_file)]

    cmd += ["-P", plugin] + expand_plugin_args(case.get("plugin_args", []))

    if case.get("monitor", True):
        cmd += ["-P", "splicemonitor", "--splice-pid", splice_pid,
                "--all-commands", "--display-commands", "--meta-sections", "--json-line"]

    cmd += ["-O", "drop"]
    return cmd


def load_cases() -> list[Path]:
    return sorted(CASES_DIR.glob("*.json"))


def case_id(path: Path) -> str:
    return path.stem


@pytest.fixture(scope="session", autouse=True)
def require_tsp():
    from shutil import which
    if which("tsp") is None:
        pytest.skip("tsp executable not found in PATH")


@pytest.mark.parametrize("case_file", load_cases(), ids=case_id)
def test_plugin(case_file: Path):
    case = json.loads(case_file.read_text(encoding="utf-8"))
    command = build_command(case)

    process = subprocess.run(
        command,
        cwd=str(TESTS_DIR),
        env=build_env(),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=case.get("timeout", DEFAULT_TIMEOUT),
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    # tsp logs on stderr, it is merged above so assertions see the whole output.
    output = process.stdout or ""
    sys.stdout.write(f"$ {subprocess.list2cmdline(command)}\n{output}\n")

    expect = case.get("expect", {})
    failures: list[str] = []

    if "exit_code" in expect:
        accepted = expect["exit_code"]
        accepted = accepted if isinstance(accepted, list) else [accepted]
        if process.returncode not in accepted:
            failures.append(f"exit code {process.returncode}, expected one of {accepted}")

    for text in expect.get("contains", []):
        if text not in output:
            failures.append(f"missing expected text: {text!r}")

    for text in expect.get("not_contains", []):
        if text in output:
            failures.append(f"unexpected text found: {text!r}")

    for pattern in expect.get("matches", []):
        if re.search(pattern, output, re.MULTILINE) is None:
            failures.append(f"no match for regex: {pattern!r}")

    for pattern in expect.get("not_matches", []):
        if re.search(pattern, output, re.MULTILINE) is not None:
            failures.append(f"unexpected match for regex: {pattern!r}")

    for pattern, expected_count in expect.get("counts", {}).items():
        found = len(re.findall(pattern, output, re.MULTILINE))
        if found != expected_count:
            failures.append(f"regex {pattern!r} matched {found} times, expected {expected_count}")

    assert not failures, (
        f"{case.get('name', case_file.stem)} failed:\n  - "
        + "\n  - ".join(failures)
        + f"\n\nCommand: {subprocess.list2cmdline(command)}\n\nOutput:\n{output}"
    )
