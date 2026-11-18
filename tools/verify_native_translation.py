"""Check translation metadata and byte-exact string emission, not gameplay equivalence."""
import hashlib
import json
from pathlib import Path
import re
import struct


def literal_bytes(value):
    out = bytearray()
    position = 0
    escapes = {"n": 10, "r": 13, "t": 9, "\\": 92, '"': 34, "0": 0}
    while position < len(value):
        char = value[position]
        position += 1
        if char != "\\":
            out.append(ord(char))
            continue
        match = re.match(r"[0-7]{1,3}", value[position:])
        if match:
            out.append(int(match[0], 8))
            position += len(match[0])
        else:
            out.append(escapes[value[position]])
            position += 1
    return bytes(out)


def main():
    original = Path("direct/game/original.ax").read_bytes()
    native = Path("direct/generated/game.cpp").read_bytes()
    source = native.decode("ascii")
    offset, size = struct.unpack_from("<II", original, 24)
    data_segment = original[offset:offset + size]
    variables = struct.unpack_from("<I", original, 8)[0]
    assert f"#define _HSP3CNV_MAXVAR {variables}" in source
    assert "PushPrgcmd(" not in source, "Unconverted IRQ directive"
    assert "DirectRegisterIrq(0,1," in source
    assert "DirectFunctionInfo(" in source
    for table in ("__HspFuncInfo", "__HspLibInfo", "__HspPrmInfo"):
        assert f"sizeof({table})" in source, "Table size must use the target ABI"
    assert not re.search(r"//[^\n]*\bPush(?:Int|Var|Str|Func)\(", source), "Native expression swallowed by a comment"
    literals = re.findall(r'PushStr\("((?:\\.|[^"\\])*)"\)', source)
    for literal in literals:
        decoded = literal_bytes(literal)
        if decoded + b"\0" not in data_segment:
            raise ValueError(f"Native string is absent from original data segment: {decoded[:40]!r}")
    doubles = re.findall(r"PushDouble\(([^)]+)\)", source)
    for literal in doubles:
        if literal.strip() == "-0":
            raise ValueError("C++ integer negative zero would lose a double's sign bit")
        if struct.pack("<d", float(literal)) not in data_segment:
            raise ValueError(f"Native double lost original precision: {literal}")
    report = {"original_sha256": hashlib.sha256(original).hexdigest(),
              "native_sha256": hashlib.sha256(native).hexdigest(), "variables": variables,
              "verified_string_emissions": len(literals), "ascii_source": True,
              "verified_double_emissions": len(doubles),
              "native_task_functions": len(re.findall(r"static void L\d+\( void \) \{", source)),
              "gameplay_equivalence_verified": False}
    Path("reports/native-translation.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
