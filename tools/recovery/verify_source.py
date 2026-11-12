"""Refuse recovery builds from a different revision or unexpected source edits."""
from pathlib import Path
import subprocess
import sys

PIN = "304f073752fd25c854e1bcf05d8e7f925b1f4e14"
sys.path.insert(0, str(Path(__file__).resolve().parent))
import patch_rkdeveloptool


def verify(source):
    def git(*arguments):
        return subprocess.check_output(["git", "-C", str(source), *arguments])
    if git("rev-parse", "HEAD").decode().strip() != PIN:
        raise ValueError("recovery source is not the pinned upstream revision")
    changes = set(git("diff", "HEAD", "--name-only").decode().splitlines())
    if changes != set(patch_rkdeveloptool.EDITS):
        raise ValueError("recovery source has missing or unexpected edits")
    for name in patch_rkdeveloptool.EDITS:
        original = git("show", f"HEAD:{name}").decode("utf-8").replace("\r\n", "\n")
        expected = patch_rkdeveloptool.patched_text(name, original)
        actual = (source / name).read_text(encoding="utf-8")
        if actual != expected:
            raise ValueError(f"{name} does not exactly match the ECHO patch")
    if (source / "config.h").exists():
        raise ValueError("source contains config.h; use a clean source checkout and Scratch build")
    print(f"Verified pinned ECHO recovery source {PIN}")


if __name__ == "__main__":
    try:
        verify(Path(sys.argv[1]).resolve())
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error))
