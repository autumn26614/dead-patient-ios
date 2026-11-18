"""Launch the directly compiled game on an ephemeral GitHub runner simulator."""
import json
import os
from pathlib import Path
import re
import subprocess
import time


# Test-only key presses (Windows virtual keys at presented ZGP frames, see
# direct/runtime/input_state.hpp): on the first-start language screen Down
# (English) then Enter, then Enter on the controller-type screen (Xinput);
# after the splash screens (title at about frame 960), X (the game's OK
# button, nyuryoku) on "PUSH ANY BUTTON", on the menu's first entry
# (Chapter1) and on the chapter menu's first entry (Start); then X on the
# prologue prompt's "Yes" and every 150 frames to advance the prologue.
INPUT_SCRIPT = ("40@300-306,13@400-406,13@600-606,88@1050-1056,88@1300-1306,88@1450-1456,88@1650-1656,"
                + ",".join("88@%d-%d" % (f, f + 6) for f in range(1800, 4200, 150)))


def command(*arguments, env=None):
    return subprocess.check_output(["xcrun", "simctl", *arguments], text=True, stderr=subprocess.STDOUT, env=env)


def main():
    output = Path("build/ios-simulator").resolve()
    app = output / "products/Release-iphonesimulator/hspproj.app"
    devices = json.loads(command("list", "devices", "available", "--json"))["devices"]
    candidates = [d for runtime, entries in devices.items() if ".iOS-" in runtime
                  for d in entries if d.get("isAvailable") and d["name"].startswith("iPhone")]
    if not candidates:
        raise RuntimeError("No installed iPhone simulator")
    device = candidates[0]
    if device["state"] != "Booted":
        command("boot", device["udid"])
    command("bootstatus", device["udid"], "-b")
    command("install", device["udid"], str(app))
    launch = command("launch", "--terminate-running-process",
                     "--stdout=" + str(output / "app-stdout.log"),
                     "--stderr=" + str(output / "app-stderr.log"),
                     device["udid"], "dev.deadpatient.port",
                     env=dict(os.environ, SIMCTL_CHILD_CP2_GRAPHICS_SELFTEST="1",
                              SIMCTL_CHILD_CP2_INPUT_SCRIPT=INPUT_SCRIPT,
                              # The runner's simulator rasterizes in software:
                              # native resolution (about 5x the pixels) runs
                              # at a third of the frame rate, so the smoke test
                              # renders at the game's 960x540.
                              SIMCTL_CHILD_CP2_RENDER_NATIVE="0"))
    (output / "launch.log").write_text(launch)
    pid = int(re.search(r": (\d+)", launch)[1])

    def running():
        try:
            os.kill(pid, 0)
            return True
        except ProcessLookupError:
            return False

    # Screenshots at 8, 20, 40, 70, 100 and then every 30 seconds up to 340 show how far startup and the
    # opening screens get (loading every model and texture takes a while);
    # direct-game.png is the last one.
    alive_after_8 = False
    started = time.monotonic()
    for seconds in (8, 20, 40, 70, 100, 130, 160, 190, 220, 250, 280, 310, 340):
        time.sleep(max(0.0, started + seconds - time.monotonic()))
        if not running():
            break
        if seconds == 8:
            alive_after_8 = True
        if seconds == 130:
            # A CPU profile (macOS sample) shows where slow frames go.
            subprocess.run(["sample", str(pid), "5", "-file", str(output / "sample-130s.txt")],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for name in ("direct-game-%ds.png" % seconds, "direct-game.png"):
            command("io", device["udid"], "screenshot", str(output / name))
            # The app is landscape; the simulator captures the portrait device
            # screen, so turn the image upright (sips ships with macOS).
            subprocess.run(["sips", "-r", "270", str(output / name)], stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL)
    alive = running()
    stderr = (output / "app-stderr.log").read_text(errors="replace") if (output / "app-stderr.log").exists() else ""
    blocked = "[CP2 native import]" in stderr or "#Error " in stderr or "[CP2 HSP runtime] error" in stderr
    blocker = next((line for line in stderr.splitlines() if line.startswith("[CP2 native import]")), None)
    runtime_error = next((line for line in stderr.splitlines() if line.startswith("[CP2 HSP runtime] error")), None)
    csmesh_registered = "native CSMesh backend registered" in stderr
    graphics_passed = "rectangle pixels and clipping: PASS" in stderr
    gl3d_passed = "transform, lighting, culling, depth and vertex blending: PASS" in stderr
    assets_passed = "Windows lookup and save round trip: PASS" in stderr
    report = {"device": device["name"], "process_alive_after_8_seconds": alive_after_8,
              "process_alive_after_340_seconds": alive, "first_runtime_error": runtime_error,
              "runtime_blocked": blocked, "graphics_driver_test_passed": graphics_passed,
              "gl3d_test_passed": gl3d_passed,
              "asset_driver_test_passed": assets_passed,
              "csmesh_backend_registered": csmesh_registered, "first_unsupported_import": blocker,
              "gameplay_verified": False, "assets_bundled": (app / "game-assets.manifest.json").is_file()}
    (output / "smoke-test.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))
    for name in ("app-stdout.log", "app-stderr.log"):
        path = output / name
        if path.exists():
            print(path.read_text(errors="replace")[-5000:])
    if alive:
        command("terminate", device["udid"], "dev.deadpatient.port")
    else:
        raise RuntimeError("Direct game process crashed or exited during startup")
    if not graphics_passed:
        raise RuntimeError("Native graphics framebuffer/pixel validation did not pass")
    if not assets_passed:
        raise RuntimeError("Original asset access/save validation did not pass")


if __name__ == "__main__":
    main()
