"""Generate a logging proxy for IDirect3D8/IDirect3DDevice8 from a mingw d3d8.h.

The proxy (direct/tests/d3d8_trace.hpp) is used only by the Windows reference
harness that drives the original plugin/zgp1807.hpi. Every call is forwarded to
the real Direct3D 8 object and logged deterministically: created objects get
sequence ids, structures and matrices are logged by value (floats as bits) and
user vertex/index data by size and FNV-1a hash.
"""
import argparse
import re
from pathlib import Path

STRUCTS = {
    "D3DMATRIX": "16f", "D3DVIEWPORT8": "6u", "D3DMATERIAL8": "17f", "D3DLIGHT8": "26x",
    "D3DPRESENT_PARAMETERS": "13u", "RECT": "4i", "D3DCLIPSTATUS8": "6u",
    # D3DX queries these while loading textures; logged so the native adapter
    # can answer identically. D3DCAPS8 is 53 DWORDs (checked by the harness).
    "D3DCAPS8": "53x", "D3DDISPLAYMODE": "4u", "D3DSURFACE_DESC": "8u",
}
OBJECTS = ("IDirect3D8", "IDirect3DDevice8", "IDirect3DSurface8", "IDirect3DTexture8", "IDirect3DBaseTexture8",
           "IDirect3DVertexBuffer8", "IDirect3DIndexBuffer8", "IDirect3DSwapChain8", "IDirect3DCubeTexture8",
           "IDirect3DVolumeTexture8")


def methods(header, interface):
    block = re.search(r"#define INTERFACE %s\s*DECLARE_INTERFACE_IID_\([^{]*\{(.*?)\};" % interface, header, re.S).group(1)
    result = []
    for m in re.finditer(r"STDMETHOD(_)?\((.*?)\)\s*\((.*?)\)\s*PURE;", block, re.S):
        head = m.group(2)
        if m.group(1):
            ret, name = [s.strip() for s in head.split(",", 1)]
        else:
            ret, name = "HRESULT", head.strip()
        params = " ".join(m.group(3).split())
        params = params.replace("THIS_", "").replace("THIS", "").strip()
        args = []
        if params:
            for p in params.split(","):
                p = p.strip()
                pname = re.search(r"(\w+)\s*$", p).group(1)
                ptype = p[: -len(pname)].strip()
                args.append((ptype, pname))
        result.append((ret, name, args))
    return result


def logger(ptype, pname, args, method=""):
    base = re.sub(r"\b(const|CONST|struct)\b", "", ptype).replace("*", "").strip()
    stars = ptype.count("*")
    # Get* methods only fill their non-const structures; the contents before
    # the call are uninitialized memory, so only the result is logged.
    if stars == 1 and base in STRUCTS and method.startswith("Get") and "const" not in ptype.lower():
        return 'log_text(%s ? "ptr" : "null");' % pname
    if stars == 0:
        if base in ("float", "FLOAT"):
            return 'log_float(%s);' % pname
        if base in ("REFIID", "REFGUID"):
            return 'log_text("iid");'
        if base == "HWND":
            return 'log_text(%s ? "hwnd" : "null");' % pname
        return 'log_uint(static_cast<unsigned long>(%s));' % pname
    if stars == 1 and base in STRUCTS:
        return 'log_struct(%s, "%s");' % (pname, STRUCTS[base])
    if stars == 1 and base in OBJECTS:
        return 'log_object(%s);' % pname
    if stars == 1 and base in ("float",) :
        return 'log_struct(%s, "4f");' % pname
    if stars == 1 and base == "D3DRECT":
        count = next((n for t, n in args if n in ("rect_count", "Count")), None)
        return 'log_rects(%s, %s);' % (pname, count or "1")
    return 'log_text(%s ? "ptr" : "null");' % pname


def after(ptype, pname):
    base = re.sub(r"\b(const|CONST|struct)\b", "", ptype).replace("*", "").strip()
    if ptype.count("*") == 2 and base in OBJECTS:
        return 'log_created(%s);' % pname
    if ptype.count("*") == 1 and base in ("DWORD", "UINT", "WINBOOL", "BOOL") and "const" not in ptype:
        return 'log_out_uint(%s);' % pname
    if ptype.count("*") == 1 and base in STRUCTS and "const" not in ptype.lower():
        return 'log_out_struct(%s, "%s");' % (pname, STRUCTS[base])
    return None


def special(name):
    if name == "DrawPrimitiveUP":
        return 'log_data(data, vertex_count_up(primitive_type, primitive_count) * stride);'
    if name == "DrawIndexedPrimitiveUP":
        return ('log_data(data, (min_vertex_idx + vertex_count) * stride); '
                'log_data(index_data, vertex_count_up(primitive_type, primitive_count) * (index_format == D3DFMT_INDEX32 ? 4 : 2));')
    return None


def generate(header, interface, cls):
    lines = ["struct %s : %s {" % (cls, interface), "    %s* real;" % interface,
             "    explicit %s(%s* r) : real(r) {}" % (cls, interface)]
    for ret, name, args in methods(header, interface):
        decl = ", ".join("%s %s" % a for a in args)
        call = ", ".join(n for _, n in args)
        body = ['begin("%s");' % name]
        if name not in ("QueryInterface", "AddRef", "Release"):
            for t, n in args:
                body.append(logger(t, n, args, name))
            extra = special(name)
            if extra:
                body.append(extra)
        if ret == "void":
            body.append("real->%s(%s);" % (name, call))
        else:
            body.append("%s result = real->%s(%s);" % (ret, name, call))
        if interface == "IDirect3DDevice8" and name == "GetDirect3D":
            # Hand D3DX the traced IDirect3D8 so its format checks are logged.
            body.append("if (SUCCEEDED(result) && *ppD3D8) *ppD3D8 = proxy_for(*ppD3D8);")
        if interface == "IDirect3D8" and name == "CreateDevice":
            body.append("if (SUCCEEDED(result) && *ppReturnedDeviceInterface) *ppReturnedDeviceInterface = wrap_device(*ppReturnedDeviceInterface);")
        for t, n in args:
            a = after(t, n)
            if a:
                body.append(a)
        if ret != "void":
            body.append("log_result((unsigned long)(ULONG_PTR)(result));")
        body.append("end();")
        if ret != "void":
            body.append("return result;")
        lines.append("    %s STDMETHODCALLTYPE %s(%s) override {" % (ret, name, decl))
        lines += ["        " + b for b in body]
        lines.append("    }")
    lines.append("};")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("header", type=Path)
    parser.add_argument("--output", type=Path, default=Path("direct/tests/d3d8_trace_generated.hpp"))
    args = parser.parse_args()
    header = args.header.read_text()
    text = ["// Generated by tools/gen_d3d8_trace.py from mingw d3d8.h. Do not edit.",
            "#pragma once",
            generate(header, "IDirect3DDevice8", "TraceDevice"),
            generate(header, "IDirect3D8", "TraceDirect3D"), ""]
    args.output.write_text("\n".join(text), encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
