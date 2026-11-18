"""Translate the original game's recovered bytecode with the patched official converter."""
import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--converter", type=Path, default=Path("build/direct/hsp3cnv-console.exe"))
    args = parser.parse_args()
    output = Path("direct/generated/game.cpp")
    output.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run([str(args.converter.resolve()), "-o" + str(output.resolve()),
                             str(Path("direct/game/original").resolve())], capture_output=True, timeout=60)
    Path("build/direct/conversion.log").write_bytes(result.stdout + result.stderr)
    if result.returncode:
        raise SystemExit((result.stdout + result.stderr).decode(errors="replace"))
    if not output.is_file():
        raise SystemExit("Converter returned without writing native game source")
    subprocess.run(["python", "tools/verify_native_translation.py"], check=True)


if __name__ == "__main__":
    main()
