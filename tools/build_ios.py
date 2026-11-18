"""Compile the directly translated game for arm64 iOS using Apple's SDK."""
import argparse
import json
from pathlib import Path
import platform
import plistlib
import shutil
import subprocess
import sys
import zipfile
from package_game_assets import verify_tree
from fetch_game_assets import MANIFEST_SHA256
import hashlib


def run(command, log=None):
    result = subprocess.run(command, capture_output=True, text=True, errors="replace")
    if log:
        Path(log).write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode:
        print((result.stdout + result.stderr)[-14000:])
        raise RuntimeError(f"Command failed ({result.returncode}): {command[0]}")
    return result.stdout.strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--simulator", action="store_true")
    parser.add_argument("--assets", type=Path, default=Path("build/game-assets"))
    parser.add_argument("--asset-manifest", type=Path, default=Path("build/game-asset-package/game-assets.manifest.json"))
    args = parser.parse_args()
    if sys.platform != "darwin":
        raise SystemExit("Run on the macOS GitHub runner; an Apple iPhoneOS SDK is required")
    root = Path.cwd()
    if hashlib.sha256(args.asset_manifest.read_bytes()).hexdigest() != MANIFEST_SHA256:
        raise RuntimeError("Original asset manifest is not the pinned version")
    assets = verify_tree(args.assets, args.asset_manifest)
    output = root / ("build/ios-simulator" if args.simulator else "build/ios")
    output.mkdir(parents=True, exist_ok=True)
    sdk_name = "iphonesimulator" if args.simulator else "iphoneos"
    sdk = run(["xcrun", "--sdk", sdk_name, "--show-sdk-path"])
    project = root / "direct/ios/hspproj"
    embed = project / "hsp3embed"
    shutil.copyfile("direct/generated/game.cpp", embed / "hspsource.cpp")
    shutil.copyfile("direct/runtime/direct_runtime.h", embed / "direct_runtime.h")
    for helper in (root / "direct/runtime").glob("*.hpp"):
        shutil.copyfile(helper, embed / helper.name)
    minimum = "-mios-simulator-version-min=15.0" if args.simulator else "-miphoneos-version-min=15.0"
    flags = ["-arch", "arm64", "-isysroot", sdk, minimum, "-std=c++14",
             "-DHSPIOS", "-DHSPDISH", "-DHSPEMBED", "-DHSP64", "-Wno-write-strings",
             "-I" + str(embed), "-I" + str(root / "direct/runtime")]
    run(["xcrun", "clang++", *flags, "-c", "direct/generated/game.cpp", "-o", str(output / "game-arm64.o")], output / "game-compile.log")
    architectures = run(["xcrun", "lipo", "-archs", str(output / "game-arm64.o")])
    if architectures != "arm64":
        raise RuntimeError(f"Unexpected native game architecture: {architectures}")
    report = {"game_object_architecture": architectures, "runner_machine": platform.machine(),
              "sdk": sdk, "gameplay_equivalence_verified": False, "playable": False,
              "assets_bundled": False,
              "limitations": ["Graphics/animation and other native imports remain incomplete", "Unsigned app requires signing before installation"]}
    (output / "compilation.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    plist_path = project / "hspproj-Info.plist"
    with plist_path.open("rb") as stream:
        plist = plistlib.load(stream)
    plist["CFBundleDisplayName"] = "Dead Patient"
    with plist_path.open("wb") as stream:
        plistlib.dump(plist, stream)
    run(["xcodebuild", "-project", "direct/ios/hspproj.xcodeproj", "-target", "hspproj",
         "-configuration", "Release", "-sdk", sdk_name, "-arch", "arm64",
         "CODE_SIGNING_ALLOWED=NO", "CODE_SIGNING_REQUIRED=NO", "CODE_SIGN_IDENTITY=",
         "IPHONEOS_DEPLOYMENT_TARGET=15.0", "PRODUCT_BUNDLE_IDENTIFIER=dev.deadpatient.port",
         "GCC_PREPROCESSOR_DEFINITIONS=HSPIOS HSPDISH HSPEMBED HSP64 USE_OBAQ",
         "CLANG_CXX_LANGUAGE_STANDARD=c++14", "CLANG_CXX_LIBRARY=libc++",
         # CoreText: the text rasterizer for mes into HSP memory bitmaps (iOSBridge.mm);
         # GameController: physical game controllers (iOSBridge.mm).
         "OTHER_LDFLAGS=$(inherited) -framework CoreText -framework GameController",
         "SYMROOT=" + str(output / "products"), "OBJROOT=" + str(output / "objects"), "build"], output / "xcodebuild.log")
    app = output / ("products/Release-" + sdk_name) / "hspproj.app"
    if not app.is_dir():
        raise RuntimeError("Xcode did not produce an app")
    # App icons beyond the template's three (tools/make_app_icon.py); the
    # plist lists them by name.
    for icon in sorted((root / "direct/ios/art/AppIcon").glob("*.png")):
        shutil.copyfile(icon, app / icon.name)
    shutil.copytree(args.assets, app / "game-assets", dirs_exist_ok=True)
    shutil.copyfile(args.asset_manifest, app / "game-assets.manifest.json")
    verify_tree(app / "game-assets", args.asset_manifest)
    report.update(assets_bundled=True, original_asset_files=assets["file_count"],
                  original_asset_bytes=assets["total_bytes"], original_asset_tree_sha256=assets["tree_sha256"])
    binary_architectures = run(["xcrun", "lipo", "-archs", str(app / "hspproj")])
    if binary_architectures != "arm64":
        raise RuntimeError(f"Unexpected app architecture: {binary_architectures}")
    if args.simulator:
        report.update(app_architecture=binary_architectures, simulator_app=str(app))
        (output / "compilation.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        return
    ipa = output / "DeadPatient-arm64-unsigned-diagnostic.ipa"
    with zipfile.ZipFile(ipa, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(app.rglob("*")):
            if path.is_file():
                archive.write(path, "Payload/hspproj.app/" + path.relative_to(app).as_posix())
    report.update(app_architecture=binary_architectures, unsigned_ipa=ipa.name)
    (output / "compilation.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
