#!/usr/bin/env python3
# Copyright(c) 2015-2026 Panos Karabelas
# Licensed under the Spartan Engine License. See license.md in the repository root.
# https://github.com/PanosK92/SpartanEngine/blob/master/license.md
# Commercial use requires written permission and negotiated payment terms.

"""Run the packaged editor's CPU-only startup check and retain CI diagnostics."""

import argparse
import html
import os
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--timeout", type=float, default=120)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    executable = args.executable.resolve()
    directory = executable.parent
    log = directory / "log.txt"
    report = directory / "smoke_test_report.txt"
    stdout = directory / "smoke_test_stdout.txt"
    stderr = directory / "smoke_test_stderr.txt"
    result = "FAIL: startup smoke test did not complete"
    passed = False
    try:
        # A stale success marker must never turn a failed launch into a pass.
        log.unlink(missing_ok=True)
        if sys.platform == "win32":
            import ctypes
            # Children inherit this: loader failures/crashes must not wait on a dialog.
            ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002 | 0x8000)
        with stdout.open("w", encoding="utf-8") as out, stderr.open("w", encoding="utf-8") as err:
            process = subprocess.run(
                [str(executable), "--ci-smoke-test"],
                cwd=directory,
                stdout=out,
                stderr=err,
                timeout=args.timeout,
                check=False,
            )
        text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
        if process.returncode != 0:
            result = f"FAIL: editor exited with code {process.returncode} (0x{process.returncode & 0xffffffff:08X})"
        elif "STARTUP_SMOKE_TEST_PASSED" not in text:
            result = "FAIL: editor exited without confirming idle ticks and clean shutdown"
        elif any(line.startswith("Error: ") for line in text.splitlines()):
            result = "FAIL: engine logged errors during startup or shutdown"
        else:
            result = "PASS: editor initialized, completed 10 idle ticks and shut down without a GPU"
            passed = True
    except subprocess.TimeoutExpired:
        result = f"FAIL: editor timed out after {args.timeout:g} seconds (process killed)"
    except OSError as error:
        result = f"FAIL: could not run startup smoke test: {error}"

    sections = [executable.name, result]
    for path in (log, stdout, stderr):
        content = path.read_text(encoding="utf-8", errors="replace") if path.exists() else "(not produced)"
        sections.extend([f"\n--- {path.name} (last 80 lines) ---", "\n".join(content.splitlines()[-80:])])
    details = "\n".join(sections) + "\n"
    report.write_text(details, encoding="utf-8")
    print(result)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as file:
            file.write(f"### Startup smoke test: {html.escape(executable.name)}\n\n")
            file.write(f"<pre>{html.escape(details[-20000:])}</pre>\n\n")
            if not passed:
                file.write("Download the smoke-test artifact for the full log.txt and process output.\n")
    if not passed and os.environ.get("GITHUB_ACTIONS") == "true":
        message = result.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
        print(f"::error title=Startup smoke test::{message}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
