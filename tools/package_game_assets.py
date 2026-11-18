"""Transport the unmodified private installation as hashed, bounded ZIP parts.

No asset decoding or executable conversion occurs here. Only Python's standard
library is needed, on Windows and on the macOS build runner.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import stat
import tempfile
import unicodedata
import zipfile

MANIFEST = "game-assets.manifest.json"
BLOCK = 1024 * 1024
RELEASE_LIMIT = 2 * 1024 ** 3


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(BLOCK), b""):
            result.update(block)
    return result.hexdigest()


def safe_name(name):
    if (not isinstance(name, str) or not name or "\\" in name or ":" in name
            or any(ord(char) < 32 for char in name)):
        raise ValueError(f"Unsafe asset path: {name!r}")
    parts = name.split("/")
    if any(part in ("", ".", "..") for part in parts) or PurePosixPath(name).is_absolute():
        raise ValueError(f"Unsafe asset path: {name!r}")
    return name


def check_names(names):
    """Reject aliasing on case-insensitive/decomposing iOS filesystems."""
    seen = {}
    files = set()
    for name in names:
        safe_name(name)
        parts = name.split("/")
        for count in range(1, len(parts) + 1):
            prefix = "/".join(parts[:count])
            key = unicodedata.normalize("NFD", prefix).casefold()
            previous = seen.setdefault(key, prefix)
            if previous != prefix:
                raise ValueError(f"Case/Unicode path collision: {previous!r}, {prefix!r}")
            if count < len(parts) and key in files:
                raise ValueError(f"Asset is both a file and directory: {prefix!r}")
        key = unicodedata.normalize("NFD", name).casefold()
        if key in files:
            raise ValueError(f"Duplicate asset: {name!r}")
        if any(other.startswith(key + "/") for other in seen):
            raise ValueError(f"Asset is both a file and directory: {name!r}")
        files.add(key)


def is_link(path):
    return path.is_symlink() or getattr(path, "is_junction", lambda: False)()


def scan(source):
    if not source.is_dir() or is_link(source):
        raise ValueError("Source must be a real installation directory")
    result = []
    # Explicit component ordering matches the original Windows package on every
    # host. pathlib's default comparison otherwise changes with the host OS.
    for path in sorted(source.rglob("*"), key=lambda path: tuple(part.lower() for part in path.relative_to(source).parts)):
        if is_link(path):
            raise ValueError(f"Links are not accepted in the installation: {path}")
        if path.is_dir():
            continue
        if not stat.S_ISREG(path.stat().st_mode):
            raise ValueError(f"Not a regular file: {path}")
        result.append({"path": path.relative_to(source).as_posix(), "bytes": path.stat().st_size})
    if not result:
        raise ValueError("Installation contains no files")
    check_names(item["path"] for item in result)
    return result


def tree_hash(files):
    payload = json.dumps([{key: item[key] for key in ("path", "bytes", "sha256")}
                          for item in files], ensure_ascii=False, separators=(",", ":"))
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


def package(source, output, max_part_bytes):
    source, output = source.resolve(), output.resolve()
    if source == output or source in output.parents or output in source.parents:
        raise ValueError("Output must be separate from the installation")
    if not 1024 * 1024 <= max_part_bytes < RELEASE_LIMIT:
        raise ValueError("Part bound must be at least 1 MiB and less than 2 GiB")
    files = scan(source)
    # Reserve space for ZIP headers and the worst-case deflate expansion. The
    # bound is checked again after compression; there is no split-file format.
    groups, current, used = [], [], 22
    for item in files:
        cost = item["bytes"] + item["bytes"] // 1000 + 1024 + 2 * len(item["path"].encode("utf-8"))
        if cost + 22 > max_part_bytes:
            raise ValueError(f"Single file exceeds the requested part bound: {item['path']}")
        if current and used + cost > max_part_bytes:
            groups.append(current)
            current, used = [], 22
        current.append(item)
        used += cost
    groups.append(current)
    output.mkdir(parents=True, exist_ok=True)
    if any(output.iterdir()):
        raise ValueError("Package output must be empty (avoid retaining stale ZIP parts)")
    parts = []
    for index, group in enumerate(groups, 1):
        name = f"game-assets-{index:03}.zip"
        target = output / name
        with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=True) as archive:
            for item in group:
                info = zipfile.ZipInfo(item["path"], date_time=(1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.create_system = 3
                info.external_attr = (stat.S_IFREG | 0o644) << 16
                hasher, total = hashlib.sha256(), 0
                with (source / item["path"]).open("rb") as incoming, archive.open(info, "w", force_zip64=True) as outgoing:
                    for block in iter(lambda: incoming.read(BLOCK), b""):
                        outgoing.write(block)
                        hasher.update(block)
                        total += len(block)
                if total != item["bytes"]:
                    raise ValueError(f"Installation changed during packaging: {item['path']}")
                item.update(sha256=hasher.hexdigest(), part=name)
        size = target.stat().st_size
        if size > max_part_bytes:
            raise ValueError(f"ZIP part exceeds bound: {name}")
        parts.append({"name": name, "bytes": size, "sha256": digest(target)})
        print(f"Packaged {name}: {size:,} bytes", flush=True)
    manifest = {"schema_version": 1, "format": "cp2-original-installation-zip-parts",
                "source_name": source.name, "file_count": len(files),
                "total_bytes": sum(item["bytes"] for item in files),
                "tree_sha256": tree_hash(files), "exclusions": [],
                "parts": parts, "files": files}
    (output / MANIFEST).write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return manifest


def read_manifest(package_dir):
    data = json.loads((package_dir / MANIFEST).read_text(encoding="utf-8"))
    if data.get("schema_version") != 1 or data.get("format") != "cp2-original-installation-zip-parts":
        raise ValueError("Unsupported asset manifest format")
    files, parts = data["files"], data["parts"]
    if not files or not parts:
        raise ValueError("Asset manifest contains no files/parts")
    check_names(item["path"] for item in files)
    part_names = [safe_name(part["name"]) for part in parts]
    if len(set(part_names)) != len(part_names) or any("/" in name for name in part_names):
        raise ValueError("Invalid ZIP part names")
    for item in [*files, *parts]:
        if type(item["bytes"]) is not int or item["bytes"] < 0:
            raise ValueError("Invalid asset byte count")
        sha = item["sha256"]
        if not isinstance(sha, str) or len(sha) != 64 or any(c not in "0123456789abcdef" for c in sha):
            raise ValueError("Invalid SHA256 in asset manifest")
    if any(item["part"] not in part_names for item in files):
        raise ValueError("Missing referenced ZIP part")
    if (len(files) != data["file_count"] or sum(item["bytes"] for item in files) != data["total_bytes"]
            or tree_hash(files) != data["tree_sha256"]):
        raise ValueError("Asset manifest tree summary differs from its records")
    return data


def check_package(package_dir, destination=None):
    """Validate every archive byte; optionally extract into a new directory."""
    package_dir = package_dir.resolve()
    data = read_manifest(package_dir)
    if destination is not None:
        destination = destination.resolve()
        if destination.exists():
            raise ValueError("Extraction destination must not already exist")
        if destination == package_dir or package_dir in destination.parents:
            raise ValueError("Extraction destination must be separate from the package")
        destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="cp2-assets-", dir=destination.parent if destination else None) as temporary:
        stage = Path(temporary) / "tree"
        if destination:
            stage.mkdir()
        for part in data["parts"]:
            path = package_dir / part["name"]
            if is_link(path) or not path.is_file() or path.stat().st_size != part["bytes"] or digest(path) != part["sha256"]:
                raise ValueError(f"ZIP part hash/length differs: {part['name']}")
            expected = {item["path"]: item for item in data["files"] if item["part"] == part["name"]}
            with zipfile.ZipFile(path) as archive:
                infos = archive.infolist()
                if len(infos) != len(expected) or {info.filename for info in infos} != set(expected):
                    raise ValueError(f"ZIP entries differ from manifest: {part['name']}")
                for info in infos:
                    item = expected[info.filename]
                    if info.is_dir() or (info.external_attr >> 16) & 0o170000 not in (0, stat.S_IFREG):
                        raise ValueError(f"ZIP entry is not a regular file: {info.filename}")
                    if info.file_size != item["bytes"]:
                        raise ValueError(f"ZIP entry length differs: {info.filename}")
                    target, outgoing = stage / info.filename, None
                    if destination:
                        target.parent.mkdir(parents=True, exist_ok=True)
                        outgoing = target.open("wb")
                    hasher, total = hashlib.sha256(), 0
                    try:
                        with archive.open(info) as incoming:
                            for block in iter(lambda: incoming.read(BLOCK), b""):
                                hasher.update(block)
                                total += len(block)
                                if outgoing:
                                    outgoing.write(block)
                    finally:
                        if outgoing:
                            outgoing.close()
                    if total != item["bytes"] or hasher.hexdigest() != item["sha256"]:
                        raise ValueError(f"Asset hash differs: {info.filename}")
            print(f"Verified {part['name']}", flush=True)
        if destination:
            os.replace(stage, destination)
    return data


def verify_tree(tree, manifest_path):
    if manifest_path.name != MANIFEST:
        raise ValueError(f"Expected manifest filename {MANIFEST}")
    data = read_manifest(manifest_path.parent)
    actual = scan(tree.resolve())
    expected = {item["path"]: item for item in data["files"]}
    if {item["path"] for item in actual} != set(expected):
        raise ValueError("Installed asset paths differ from manifest")
    for item in actual:
        record = expected[item["path"]]
        if item["bytes"] != record["bytes"] or digest(tree / item["path"]) != record["sha256"]:
            raise ValueError(f"Installed asset differs: {item['path']}")
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    create = commands.add_parser("create")
    create.add_argument("source", type=Path)
    create.add_argument("--output", type=Path, default=Path("build/game-asset-package"))
    create.add_argument("--max-part-mib", type=int, default=1280)
    verify = commands.add_parser("verify")
    verify.add_argument("package", type=Path)
    extract = commands.add_parser("extract")
    extract.add_argument("package", type=Path)
    extract.add_argument("--output", type=Path, required=True)
    tree = commands.add_parser("verify-tree")
    tree.add_argument("tree", type=Path)
    tree.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "create":
            data = package(args.source, args.output, args.max_part_mib * 1024 ** 2)
        elif args.command == "verify-tree":
            data = verify_tree(args.tree, args.manifest)
        else:
            data = check_package(args.package, args.output if args.command == "extract" else None)
    except (ValueError, OSError, KeyError, zipfile.BadZipFile) as error:
        parser.exit(1, f"Asset package error: {error}\n")
    print(json.dumps({key: data[key] for key in ("file_count", "total_bytes", "tree_sha256")}))


if __name__ == "__main__":
    main()
