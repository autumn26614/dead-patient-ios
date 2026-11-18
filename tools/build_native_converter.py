"""Build a diagnostic HSP3CNV without blocking Windows error dialogs."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, required=True)
    parser.add_argument("--compiler", default="clang++")
    args = parser.parse_args()
    destination = Path("build/cnv-source")
    for folder in ("hsp3cnv", "hspcmp", "hsp3"):
        shutil.copytree(args.upstream / "src" / folder, destination / folder, dirs_exist_ok=True)
    support = destination / "hsp3cnv/supio.cpp"
    data = support.read_bytes()
    data, count = re.subn(rb'MessageBox\( NULL, (mes|ss), "error",MB_ICONINFORMATION \| MB_OK \);',
                         lambda m: b'fprintf(stderr, "CONVERSION ERROR: %s\\n", ' + m[1] + b'); fflush(stderr); exit(3);', data)
    if count != 2:
        raise ValueError("Upstream diagnostic patch no longer matches")
    support.write_bytes(data)
    source = destination / "hsp3cnv/main.cpp"
    data = source.read_bytes().replace(b'i = hsp3.LoadObjectFile( fname );',
                                     b'fprintf(stderr, "Loading object\\n"); i = hsp3.LoadObjectFile( fname );')
    data = data.replace(b'hsp3.MakeSource( 0, NULL );',
                        b'fprintf(stderr, "Converting object\\n"); i = hsp3.MakeSource( 0, NULL ); if (i) return 4;')
    source.write_bytes(data)
    source = destination / "hsp3cnv/chsp3cpp.cpp"
    data = source.read_bytes()
    # This pass only pretty-prints the HSP condition into a C++ comment. Its older
    # expression parser rejects valid bytecode before native generation starts.
    comment_pass = (b'GetContext( &ctxbak );\r\n\t\t\t\tOutLine( "// if " );\r\n'
                    b'\t\t\t\tmcs++;\r\n\t\t\t\tgetCS();\r\n'
                    b'\t\t\t\tMakeProgramInfoParam2();\r\n\t\t\t\tSetContext( &ctxbak );')
    if comment_pass not in data:
        comment_pass = comment_pass.replace(b"\r\n", b"\n")
    if comment_pass not in data:
        raise ValueError("Upstream comment parser patch no longer matches")
    data = data.replace(comment_pass, b'OutLine("// if (condition generated below)"); OutCR();')
    data = data.replace(b"MakeProgramInfoParam2();", b"OutCR();")
    start = data.index(b"void CHsp3Cpp::MakeCppStyleString(")
    end = data.index(b"char *CHsp3Cpp::GetDS_cpp(", start)
    escaping = r'''void CHsp3Cpp::MakeCppStyleString(char *str, char *dst)
{
    const unsigned char *p = (const unsigned char *)str;
    while (*p) {
        unsigned char c = *p++;
        if (c < 32 || c >= 127) {
            *dst++ = '\\'; *dst++ = '0' + ((c >> 6) & 7);
            *dst++ = '0' + ((c >> 3) & 7); *dst++ = '0' + (c & 7);
        } else {
            if (c == '\\' || c == '"') *dst++ = '\\';
            *dst++ = c;
        }
    }
    *dst = 0;
}

'''.replace("'\\\\'", "'\\'")
    # The C++ backslash character must be spelled '\\', not a source escape.
    escaping = escaping.replace("'\\'", "'\\\\'")
    data = data[:start] + escaping.encode("ascii") + data[end:]
    data = data.replace(b"Push%s(%f);", b"Push%s(%.17g);")
    signature = b"void CHsp3Cpp::MakeCPPSub( int cmdtype, int cmdval )"
    start = data.index(signature)
    insert = data.index(b"int pnum;", start)
    irq = r'''if (cmdtype == TYPE_INTCMD && cmdval >= 0 && cmdval <= 3) {
        MCSCONTEXT saved; GetContext(&saved); getCS();
        if (cstype == TYPE_PROGCMD && csval <= 1) {
            int option = csval; getCS();
            if (cstype != TYPE_LABEL) { Alert("Dynamic IRQ label requires native conversion support"); return; }
            OutLine("DirectRegisterIrq(%d,%d,%d);", cmdval, option, csval); OutCR();
            getCS(); return;
        }
        SetContext(&saved);
    }
    '''
    data = data[:insert] + irq.encode("ascii") + data[insert:]
    data = data.replace(b'#include \\"hsp3r.h\\"', b'#include \\"direct_runtime.h\\"')
    data = data.replace(b'{ %d,%d, %d,%d,%d,%d,%d, (void *)%d },',
                        b'DirectFunctionInfo(%d,%d,%d,%d,%d,%d,%d,%d),')
    for table, setter in ((b"__HspFuncInfo", b"SetFInfo"), (b"__HspLibInfo", b"SetLInfo"), (b"__HspPrmInfo", b"SetMInfo")):
        data = data.replace(setter + b"( " + table + b", %d );", setter + b"( " + table + b", (int)sizeof(" + table + b") );")
    source.write_bytes(data)
    header = destination / "hsp3cnv/chsp3.h"
    header.write_bytes(header.read_bytes().replace(b"char strtmp[4096];", b"char strtmp[65536];"))
    output = Path("build/direct")
    output.mkdir(parents=True, exist_ok=True)
    files = [str(p) for p in sorted((destination / "hsp3cnv").glob("*.cpp"))]
    files += [str(destination / "hspcmp" / name) for name in ("label.cpp", "localinfo.cpp")]
    command = [args.compiler, "--target=i686-w64-windows-gnu", "-DHSPWIN", "-std=c++11",
               "-Wno-write-strings", "-Wno-c++11-narrowing", "-w", *files,
               "-static", "-o", str(output / "hsp3cnv-console.exe"), "-luser32", "-lkernel32"]
    result = subprocess.run(command, capture_output=True)
    (output / "converter-build.log").write_bytes(result.stdout + result.stderr)
    print((result.stdout + result.stderr).decode(errors="replace")[-6000:])
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
