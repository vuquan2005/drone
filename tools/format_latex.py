"""Adapt latexindent output to pre-commit without leaving logs or backups."""

import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+", type=Path)
    args = parser.parse_args()
    if shutil.which("latexindent") is None:
        parser.exit(1, "Missing latexindent; install it with your TeX distribution.\n")
    failed = False
    with tempfile.TemporaryDirectory(prefix="drone-latexindent-") as temporary:
        for path in args.files:
            result = subprocess.run(
                ["latexindent", f"-c={temporary}", str(path)], capture_output=True
            )
            if result.returncode:
                print(f"latexindent failed: {path}")
                print(result.stderr.decode("utf-8", errors="replace"))
                failed = True
            elif result.stdout != path.read_bytes():
                path.write_bytes(result.stdout)
                print(f"Formatted: {path}")
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
