import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

import echo_debug

EXCLUDED_STATE = {"library.db", "covers.bin", "build.tmp", "view.tmp", "debug"}


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def backup_state(source, destination):
    if not source.is_dir():
        raise ValueError(f"Missing user state: {source}")
    destination.mkdir(parents=True, exist_ok=False)
    hashes = {}
    for path in sorted(source.rglob("*")):
        relative = path.relative_to(source)
        if any(part in EXCLUDED_STATE or part.startswith("._") for part in relative.parts):
            continue
        if path.is_symlink():
            raise ValueError(f"Refusing state symlink: {relative}")
        if not path.is_file():
            continue
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        before = digest(path)
        shutil.copyfile(path, target)
        if digest(target) != before or digest(path) != before:
            raise ValueError(f"State backup verification failed: {relative}")
        hashes[str(relative)] = before
    if not hashes:
        raise ValueError("No persistent state files found to back up")
    write_json(destination.parent / "sha256.json", hashes)
    return hashes


def check_report(report, build, previous=None):
    if report.get("truncated"):
        raise ValueError("Restart report is truncated")
    values = report["values"]
    if values.get("firmware.build") != build:
        raise ValueError("Restarted player does not report the candidate build")
    for key in ("fault.cfsr", "fault.hfsr", "fault.pc", "audio.output_underruns"):
        if values.get(key) != 0:
            raise ValueError(f"Restart health check failed: {key}={values.get(key)!r}")
    if previous is not None and report == previous:
        raise ValueError("Restart report is unchanged")


class Release:
    def __init__(self, root, scratch, env, python, debug=None, proof=None, backup_root=None):
        self.root = Path(root)
        self.scratch = Path(scratch)
        self.env = env
        self.python = Path(python)
        self.debug = debug if debug is not None else echo_debug.Tool()
        self.proof = proof or self.prove
        self.backup_root = Path(backup_root) if backup_root is not None else self.root / ".local/device-backups/user-state"

    def git(self, *arguments):
        return subprocess.check_output(["git", *arguments], cwd=self.root, env=self.env, text=True).strip()

    def clean_head(self):
        if self.git("status", "--porcelain", "--untracked-files=all"):
            raise ValueError("Working tree must be clean, including untracked files")
        return self.git("rev-parse", "HEAD")

    def resolve(self, build):
        if not isinstance(build, str) or not re.fullmatch(r"[0-9a-f]{7,40}", build):
            raise ValueError(f"Installed build is not a clean Git build: {build!r}")
        return self.git("rev-parse", "--verify", f"{build}^{{commit}}")

    def build(self, root):
        subprocess.run([str(self.python), str(root / "tools/firmware.py"), "build", "--preset", "device",
                        "--target", "echo_device_image", "--jobs", "2"], cwd=root, env=self.env, check=True)
        return root / "firmware/build/device"

    def validate_candidate(self, candidate, sha):
        manifest = json.loads((candidate / "candidate.json").read_text())
        if manifest["commit"] != sha or not manifest["files"]:
            raise ValueError("Frozen candidate commit does not match")
        paths = {str(path.relative_to(candidate)) for path in candidate.rglob("*")
                 if path.is_file() and (path.suffix in (".elf", ".map") or path.name == echo_debug.UPDATE_FILE)}
        if paths != set(manifest["files"]):
            raise ValueError("Frozen candidate file set changed")
        for name, expected in manifest["files"].items():
            path = candidate / name
            if not path.resolve().is_relative_to(candidate.resolve()) or digest(path) != expected:
                raise ValueError(f"Frozen candidate checksum failed: {name}")
        build = echo_debug.image_build((candidate / echo_debug.UPDATE_FILE).read_bytes())
        if self.resolve(build) != sha:
            raise ValueError("Candidate image build does not match clean HEAD")
        return manifest

    def freeze(self, build, sha):
        candidate = self.scratch / "reports" / f"candidate-{sha}"
        if candidate.exists():
            self.validate_candidate(candidate, sha)
            if digest(build / echo_debug.UPDATE_FILE) != digest(candidate / echo_debug.UPDATE_FILE):
                raise ValueError("A different image is already frozen for this commit; preserve it")
            return candidate
        image = build / echo_debug.UPDATE_FILE
        if self.resolve(echo_debug.image_build(image.read_bytes())) != sha:
            raise ValueError("Candidate image build does not match clean HEAD")
        artifacts = [image, *sorted(build.rglob("*.elf")), *sorted(build.rglob("*.map"))]
        for name in ("echo_device", "echo_usb_overlay", "echo_rescan_overlay"):
            for suffix in (".elf", ".map"):
                if not (build / "platform/device" / (name + suffix)).is_file():
                    raise ValueError(f"Missing candidate artifact: {name}{suffix}")
        candidate.parent.mkdir(parents=True, exist_ok=True)
        staging = Path(tempfile.mkdtemp(prefix="candidate-", dir=self.scratch / "tmp"))
        try:
            hashes = {}
            for path in artifacts:
                relative = path.relative_to(build)
                target = staging / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                before = digest(path)
                shutil.copyfile(path, target)
                if digest(target) != before or digest(path) != before:
                    raise ValueError(f"Candidate changed while freezing: {relative}")
                hashes[str(relative)] = before
            write_json(staging / "candidate.json", {"commit": sha, "files": hashes})
            staging.rename(candidate)
        finally:
            if staging.exists():
                shutil.rmtree(staging)
        self.validate_candidate(candidate, sha)
        return candidate

    def installed_candidate(self, sha):
        candidate = self.scratch / "reports" / f"candidate-{sha}"
        if candidate.exists():
            self.validate_candidate(candidate, sha)
            return candidate
        self.scratch.joinpath("tmp").mkdir(parents=True, exist_ok=True)
        temporary = Path(tempfile.mkdtemp(prefix="installed-", dir=self.scratch / "tmp"))
        checkout = temporary / (temporary.name + "-source")
        try:
            self.git("worktree", "add", "--detach", str(checkout), sha)
            return self.freeze(self.build(checkout), sha)
        finally:
            if checkout.exists():
                self.git("worktree", "remove", "--force", str(checkout))
            output = self.scratch / "build" / checkout.name
            if output.is_dir() and not output.is_symlink():
                shutil.rmtree(output)
            build_temporary = self.scratch / "tmp" / checkout.name
            if build_temporary.is_dir() and not build_temporary.is_symlink():
                shutil.rmtree(build_temporary)
            shutil.rmtree(temporary)

    def prove(self, old, new):
        result = subprocess.check_output([str(self.python), str(self.root / "firmware/tools/prove_upgrade.py"),
                                          "--old", str(old), "--new", str(new)],
                                         cwd=self.root, env=self.env, text=True)
        return json.loads(result)

    def execute(self, dry_run=False, owner_approved=False):
        if not os.path.ismount("/Volumes/Scratch"):
            raise ValueError("Scratch is not mounted")
        if not dry_run and not owner_approved:
            raise ValueError("Owner approval for this install and exact-model recovery readiness are required; use --owner-approved")
        sha = self.clean_head()
        found = self.debug.status()
        if not found.get("connected") or not found.get("volume"):
            raise ValueError("Player is not connected with its internal volume mounted")
        previous = self.debug.report()["report"]
        installed_build = previous["values"].get("firmware.build")
        installed_sha = self.resolve(installed_build)
        if installed_sha == sha:
            raise ValueError("Candidate is already installed")
        print(f"Installed {installed_build} ({installed_sha}); candidate {sha}", flush=True)
        self.scratch.joinpath("tmp").mkdir(parents=True, exist_ok=True)
        candidate = self.freeze(self.build(self.root), sha)
        old = self.installed_candidate(installed_sha)
        proof = self.proof(old, candidate)
        if not proof.get("ok"):
            raise ValueError("Upgrade proof failed")
        if self.clean_head() != sha:
            raise ValueError("HEAD changed during candidate preparation")
        self.validate_candidate(candidate, sha)
        if self.debug.status() != found:
            raise ValueError("Player connection or status changed during preparation; retry")
        if self.debug.report()["report"]["values"].get("firmware.build") != installed_build:
            raise ValueError("Installed build changed during preparation")
        stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
        backup = self.backup_root / stamp / echo_debug.DATA_DIRECTORY
        hashes = backup_state(Path(found["volume"]) / echo_debug.DATA_DIRECTORY, backup)
        print(f"Exact upgrade proof passed; state backup verified ({len(hashes)} files): {backup}", flush=True)
        result = {"commit": sha, "installed_commit": installed_sha, "installed_build": installed_build,
                  "dry_run": dry_run, "proof": proof, "backup": str(backup), "backup_sha256": hashes,
                  "candidate": str(candidate), "ok": False, "install_attempted": False, "utc": stamp}
        try:
            if not dry_run:
                result["install_attempted"] = True
                write_json(candidate / "install.json", result)
                result["install"] = self.debug.install(candidate / echo_debug.UPDATE_FILE, restart=True, timeout=90)
                if not result["install"].get("ok") or not result["install"].get("waited"):
                    raise ValueError("Install did not confirm restart and reconnect")
                report = self.debug.report()["report"]
                result["report"] = report
                build = echo_debug.image_build((candidate / echo_debug.UPDATE_FILE).read_bytes())
                check_report(report, build, previous)
            result["ok"] = True
        except Exception as error:
            result["error"] = str(error)
            raise
        finally:
            summary = (f"{'DRY RUN' if dry_run else 'INSTALL'} {'PASS' if result['ok'] else 'FAIL'} "
                       f"{installed_build} -> {sha[:7]}; {len(hashes)} state files verified; {candidate}")
            write_json(candidate / "install.json", result)
            (candidate / "summary.txt").write_text(summary + "\n")
            print(summary, flush=True)
        return result
