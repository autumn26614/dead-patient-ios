"""Translate a straight-line x87 routine (llvm-objdump AT&T output) into C++
float expressions in the same operation order.

With the x87 precision control in single precision (Direct3D's default),
every arithmetic result is rounded to a 24-bit significand, which is what
single-precision C++ arithmetic (without contraction) gives. Values are
tracked symbolically: stack registers and memory slots hold expression
strings, and every store to memory becomes a named temporary so expressions
stay readable and evaluation order is fixed.

Supported: flds/fsts/fstps on memory, fld/fstp on st(i), fld1, fldz, fmul,
fmuls, fadd(s), faddp, fsub(s), fsubp, fsubrp, fdiv, fdivr(s), fchs, fxch,
and integer movl copies between memory and registers. Mnemonics follow GNU
as: with an st(i) destination, fsubp/fsubrp/fdivp/fdivrp are swapped
relative to Intel (fsubrp %st, %st(1) computes st(1) - st(0)).

Usage: python tools/x87_to_cpp.py ROUTINE.asm START END > out.cpp
where memory operands relative to pointer registers are named from
--pointer REG=NAME at the first instruction (e.g. --pointer 0x10(%ebp)=m).
"""
import argparse
import re
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("asm")
    ap.add_argument("start", help="first address (hex)")
    ap.add_argument("end", help="address after the last instruction (hex)")
    ap.add_argument("--pointer", action="append", default=[],
                    help="frame slot holding a pointer argument, e.g. 0x10(%%ebp)=m")
    ap.add_argument("--float", action="append", default=[],
                    help="frame slot holding a float argument, e.g. 0xc(%%ebp)=yaw")
    args = ap.parse_args()
    start, end = int(args.start, 16), int(args.end, 16)

    mem = {}       # ('frame', off) or (pointer_name, off) -> expression / ('ptr', name)
    regs = {}      # integer registers -> expression or ('ptr', name)
    for spec in args.pointer:
        slot, name = spec.split("=")
        mem[("frame", int(slot.split("(")[0], 16))] = ("ptr", name)
    for spec in args.float:
        slot, name = spec.split("=")
        mem[("frame", int(slot.split("(")[0], 16))] = name
    st = []        # x87 stack, st[0] = top
    out = []
    temps = [0]

    def address(operand):
        m = re.match(r"(-?0x[0-9a-f]+|-?\d+)?\(%(\w+)\)$", operand)
        if not m:
            raise SystemExit("unsupported operand " + operand)
        off = int(m.group(1), 0) if m.group(1) else 0
        base = m.group(2)
        if base == "ebp":
            return ("frame", off)
        ptr = regs.get(base)
        if not (isinstance(ptr, tuple) and ptr[0] == "ptr"):
            raise SystemExit("register %s does not hold a known pointer" % base)
        return (ptr[1], off)

    def load(operand):
        key = address(operand)
        if key[0] != "frame":
            return "%s[%d]" % (key[0], key[1] // 4)
        if key not in mem:
            raise SystemExit("read of an unknown frame slot %s" % operand)
        return mem[key]

    def store(operand, value):
        key = address(operand)
        if key[0] != "frame":
            out.append("    %s[%d] = %s;" % (key[0], key[1] // 4, value))
            return
        name = "t%d" % temps[0]
        temps[0] += 1
        out.append("    const float %s = %s;" % (name, value))
        mem[key] = name

    def sti(operand):
        m = re.match(r"%st(?:\((\d)\))?$", operand)
        if not m:
            return None
        return int(m.group(1) or 0)

    for line in open(args.asm, encoding="utf-8", errors="replace"):
        m = re.match(r"\s*([0-9a-f]+):\s+(\S+)\s*(.*)$", line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        if addr < start or addr >= end:
            continue
        op, rest = m.group(2), m.group(3).split("#")[0].strip()
        ops = [o.strip() for o in re.split(r",(?![^(]*\))", rest)] if rest else []
        if op == "movl":
            src, dst = ops
            if src.startswith("%"):
                val = regs.get(src[1:])
            elif src.startswith("$"):
                val = src
            else:
                key = address(src)
                val = mem.get(key) if key[0] == "frame" else "%s[%d]" % (key[0], key[1] // 4)
            if dst.startswith("%"):
                regs[dst[1:]] = val
            else:
                key = address(dst)
                if key[0] == "frame":
                    mem[key] = val
                else:
                    out.append("    %s[%d] = %s;" % (key[0], key[1] // 4, val))
            continue
        if op in ("flds",):
            st.insert(0, load(ops[0]))
        elif op == "fld":
            st.insert(0, st[sti(ops[0])])
        elif op == "fld1":
            st.insert(0, "1.0f")
        elif op == "fldz":
            st.insert(0, "0.0f")
        elif op in ("fstps", "fsts"):
            store(ops[0], st[0])
            if op == "fstps":
                st.pop(0)
        elif op == "fstp":
            i = sti(ops[0])
            st[i] = st[0]
            st.pop(0)
        elif op == "fchs":
            st[0] = "(-%s)" % st[0]
        elif op == "fxch":
            i = sti(ops[0]) if ops else 1
            st[0], st[i] = st[i], st[0]
        elif op in ("fmuls", "fadds", "fsubs", "fdivs", "fsubrs", "fdivrs"):
            v = load(ops[0])
            sym = {"fmuls": "*", "fadds": "+", "fsubs": "-", "fdivs": "/", "fsubrs": "-", "fdivrs": "/"}[op]
            st[0] = "(%s %s %s)" % ((v, sym, st[0]) if op in ("fsubrs", "fdivrs") else (st[0], sym, v))
        elif op in ("fmul", "fadd", "fsub", "fdiv", "fsubr", "fdivr"):
            a, b = sti(ops[0]), sti(ops[1])
            sym = {"fmul": "*", "fadd": "+", "fsub": "-", "fdiv": "/", "fsubr": "-", "fdivr": "/"}[op]
            if b == 0:   # st0 = st0 op st(a) (fsubr/fdivr: st(a) op st0)
                st[0] = "(%s %s %s)" % ((st[a], sym, st[0]) if op in ("fsubr", "fdivr") else (st[0], sym, st[a]))
            else:        # dest st(b), GNU as swaps sub/subr and div/divr here
                rev = op in ("fsub", "fdiv")
                st[b] = "(%s %s %s)" % ((st[0], sym, st[b]) if rev else (st[b], sym, st[0]))
        elif op in ("fmulp", "faddp", "fsubp", "fsubrp", "fdivp", "fdivrp"):
            b = sti(ops[1]) if len(ops) > 1 else 1
            sym = {"fmulp": "*", "faddp": "+", "fsubp": "-", "fsubrp": "-", "fdivp": "/", "fdivrp": "/"}[op]
            rev = op in ("fsubp", "fdivp")   # GNU: fsubp = Intel fsubrp
            st[b] = "(%s %s %s)" % ((st[0], sym, st[b]) if rev else (st[b], sym, st[0]))
            st.pop(0)
        elif op in ("pushl", "popl", "subl", "leave", "retl", "testl", "je", "jne", "calll", "fstpl"):
            out.append("    // %s %s" % (op, rest))
        else:
            raise SystemExit("unsupported instruction %s %s at %x" % (op, rest, addr))
    print("\n".join(out))
    print("    // x87 stack at the end: %s" % st)


if __name__ == "__main__":
    main()
