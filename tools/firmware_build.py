"""Resolve an image's build field to its source commit in the local repository."""

import re
import subprocess

COMMIT = re.compile(r"[0-9a-fA-F]{7,40}")
LABEL_PATTERN = r"[0-9A-Za-z][0-9A-Za-z.]{0,7}"
LABEL = re.compile(LABEL_PATTERN)


def resolve(git, build, version):
    if isinstance(build, str) and COMMIT.fullmatch(build):
        revision = build
        missing = f"Firmware commit {build!r} cannot be resolved in this repository"
    elif isinstance(build, str) and LABEL.fullmatch(build):
        tag = f"v{version}-{build}"
        revision = f"refs/tags/{tag}"
        missing = f"Firmware build {build!r} requires local tag {tag!r} pointing to a commit"
    else:
        raise ValueError(f"Firmware build {build!r} is not a clean commit or a pre-release label")

    try:
        return git("rev-parse", "--verify", "--quiet", revision + "^{commit}").strip()
    except (ValueError, subprocess.CalledProcessError) as error:
        raise ValueError(missing) from error
