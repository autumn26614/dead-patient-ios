"""Fetch and verify the pinned original assets from the authorized private release."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess

from package_game_assets import MANIFEST, check_package, verify_tree

MANIFEST_SHA256 = "373e0ffdf46e2fc0c54c069635e66a738d688a546cd9815445a128220376c1b5"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    # owner/name of the repository whose release holds the assets (or ASSET_REPO).
    parser.add_argument("--repo", default=os.environ.get("ASSET_REPO"))
    parser.add_argument("--package", type=Path, default=Path("build/game-asset-package"))
    parser.add_argument("--output", type=Path, default=Path("build/game-assets"))
    args = parser.parse_args()
    if not args.repo:
        parser.error("--repo (or ASSET_REPO) is required")
    args.package.mkdir(parents=True, exist_ok=True)
    manifest = args.package / MANIFEST
    if not manifest.exists():
        # gh uses the runner's read-only contents token. No credentials enter
        # an artifact, manifest, command-line argument or build log.
        subprocess.run(["gh", "release", "download", "game-assets-v1", "--repo", args.repo,
                        "--dir", str(args.package), "--pattern", "game-assets*"], check=True)
    if hashlib.sha256(manifest.read_bytes()).hexdigest() != MANIFEST_SHA256:
        raise RuntimeError("Original asset manifest differs from the pinned release")
    if args.output.exists():
        data = verify_tree(args.output, manifest)
    else:
        data = check_package(args.package, args.output)
    print(f"Original assets verified: {data['file_count']} files, {data['total_bytes']} bytes")


if __name__ == "__main__":
    main()
