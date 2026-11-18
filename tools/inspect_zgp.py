"""Audit the original ZGP PE exports and recovered HSP call sites without loading DLLs."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess


class PE32:
    def __init__(self, path):
        self.path = Path(path)
        self.data = self.path.read_bytes()
        if self.data[:2] != b"MZ":
            raise ValueError("Not a PE file")
        pe = self.u32(0x3c)
        if self.data[pe:pe + 4] != b"PE\0\0":
            raise ValueError("Invalid PE signature")
        self.machine, count = struct.unpack_from("<HH", self.data, pe + 4)
        optional = pe + 24
        if struct.unpack_from("<H", self.data, optional)[0] != 0x10b:
            raise ValueError("Expected PE32 optional header")
        self.image_base = self.u32(optional + 28)
        self.export_rva, self.export_size = struct.unpack_from("<II", self.data, optional + 96)
        section = optional + struct.unpack_from("<H", self.data, pe + 20)[0]
        self.sections = []
        for i in range(count):
            entry = section + i * 40
            virtual_size, rva, raw_size, offset = struct.unpack_from("<IIII", self.data, entry + 8)
            self.sections.append((rva, max(virtual_size, raw_size), offset, raw_size))

    def u32(self, offset):
        return struct.unpack_from("<I", self.data, offset)[0]

    def offset(self, rva):
        for base, size, offset, raw_size in self.sections:
            if base <= rva < base + size:
                delta = rva - base
                if delta >= raw_size:
                    raise ValueError("RVA points into uninitialized section data")
                return offset + delta
        raise ValueError(f"Unmapped RVA {rva:#x}")

    def string(self, rva):
        begin = self.offset(rva)
        return self.data[begin:self.data.index(b"\0", begin)].decode("ascii")

    def exports(self):
        table = self.offset(self.export_rva)
        ordinal_base, function_count, name_count, function_rva, name_rva, ordinal_rva = struct.unpack_from("<IIIIII", self.data, table + 16)
        result = {}
        for i in range(name_count):
            name = self.string(self.u32(self.offset(name_rva) + i * 4))
            ordinal = struct.unpack_from("<H", self.data, self.offset(ordinal_rva) + i * 2)[0]
            if ordinal >= function_count:
                raise ValueError("Export ordinal outside address table")
            rva = self.u32(self.offset(function_rva) + ordinal * 4)
            result[name] = {"ordinal": ordinal_base + ordinal, "rva": hex(rva), "va": hex(self.image_base + rva)}
        return result

    def report(self):
        return {"path": self.path.as_posix(), "sha256": hashlib.sha256(self.data).hexdigest(),
                "machine": hex(self.machine), "size": len(self.data), "exports": self.exports()}


def game_declarations(source):
    result = []
    current_library = None
    for line_no, line in enumerate(source.splitlines(), 1):
        library = re.match(r'#uselib\s+"([^"]+)"', line)
        if library:
            current_library = library.group(1)
        declaration = re.match(r'#(?:c?func)\s+(\S+)\s+"([^"]+)"\s*(.*)', line)
        if declaration and current_library and "zgp" in current_library.lower():
            name, export, signature = declaration.groups()
            result.append({"name": name, "export": export, "signature": signature, "declaration_line": line_no,
                           "call_sites": [{"line": n, "arguments": text.strip()[len(name):].strip()}
                                          for n, text in enumerate(source.splitlines(), 1)
                                          if re.match(r"\s*" + re.escape(name) + r"(?:\s|$)", text)]})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--installation", type=Path, default=Path("Corpse Party 2 Dead Patient"))
    parser.add_argument("--source", type=Path, default=Path("direct/game/recovered.hsp"))
    parser.add_argument("--output", type=Path, default=Path("build/zgp-audit.json"))
    parser.add_argument("--disassembly-dir", type=Path, help="Optional llvm-objdump evidence directory")
    args = parser.parse_args()
    zgp = PE32(args.installation / "plugin/zgp1807.hpi")
    mesh = PE32(args.installation / "plugin/CSMesh.dll")
    declarations = game_declarations(args.source.read_text(encoding="cp932"))
    exports = zgp.exports()
    missing = [entry["export"] for entry in declarations if entry["export"] not in exports]
    report = {"zgp": zgp.report(), "csmesh": mesh.report(), "game_declarations": declarations,
              "imported_zgp_functions": len(declarations), "missing_exports": missing,
              "call_site_count": sum(len(entry["call_sites"]) for entry in declarations),
              "runtime_reachability_verified": False}
    if missing:
        raise ValueError(f"Game imports missing from original ZGP: {missing}")
    if args.disassembly_dir:
        args.disassembly_dir.mkdir(parents=True, exist_ok=True)
        ordered = sorted(exports.items(), key=lambda pair: int(pair[1]["rva"], 16))
        core = {"createdx", "createdevm", "setbmscr", "anilibload", "texinfoex", "setcolor4", "setmaxunit", "setmaxsurf", "setmaxangle", "set2d", "set3d", "fill2d", "line2d", "bscene", "escene", "z_sin", "z_cos", "z_scos", "z_adif", "z_len", "z_len3", "z_dir"}
        for i, (name, entry) in enumerate(ordered):
            if name.split("@")[0].lstrip("_") not in core:
                continue
            start = int(entry["va"], 16)
            stop = int(ordered[i + 1][1]["va"], 16) if i + 1 < len(ordered) else start + 128
            result = subprocess.run(["llvm-objdump", "-d", f"--start-address={start:#x}", f"--stop-address={stop:#x}", str(zgp.path)], capture_output=True, text=True, check=True)
            (args.disassembly_dir / (name.replace("@", "_") + ".txt")).write_text(result.stdout, encoding="utf-8")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(args.output), "zgp_exports": len(exports), "csmesh_exports": len(mesh.exports()),
                      "game_imports": len(declarations), "call_sites": report["call_site_count"], "missing_exports": missing}))


if __name__ == "__main__":
    main()
