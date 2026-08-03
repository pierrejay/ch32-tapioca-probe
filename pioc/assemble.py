#!/usr/bin/env python3
"""Assemble PIOC sources and verify or update their generated C blobs.

    python3 assemble.py                 # assemble tapioca_swd and check its header
    python3 assemble.py foo.ASM         # check foo.ASM against foo_inc.h
    python3 assemble.py foo.ASM --write # (re)generate foo_inc.h from foo.ASM

The opcode table is intentionally restricted to encodings verified against WCH
EVT listings, the RISC8B manual, or the independent andelf/pioc ISA table.
Unsupported mnemonics fail instead of emitting a guessed encoding.
"""
import sys
import re
import os

# ---- opcode encoders: mnemonic -> (arity, fn(args:list[int]) -> word) --------
# [V] = byte-verified against the WCH toolchain; [M] = RISC8B manual only;
# [M+] = manual + corroborated by the independent andelf/pioc ISA table.
ENC = {
    "DW":    (1, lambda a: a[0]),                       # [V] raw word
    "JMP":   (1, lambda a: 0x6000 | a[0]),              # [V]
    "JNZ":   (1, lambda a: 0x3000 | a[0]),              # [M]
    "JZ":    (1, lambda a: 0x3400 | a[0]),              # [V-WCH]
    "CALL":  (1, lambda a: 0x7000 | a[0]),              # [M]
    "RET":   (0, lambda _: 0x0030),                     # [M+] andelf: 00000000 001100xx
    "CLR":   (1, lambda a: 0x0100 | a[0]),              # [V]
    "INC":   (2, lambda a: 0x0400 | (a[1] << 12) | a[0]),# [V] INC f,d
    "DEC":   (2, lambda a: 0x0500 | (a[1] << 12) | a[0]),# [M] DEC f,d
    "DECSZ": (2, lambda a: 0x0700 | (a[1] << 12) | a[0]),# [V-WCH]
    "RCL":   (2, lambda a: 0x0E00 | (a[1] << 12) | a[0]),# [V] RCL f,d
    "MOV":   (2, lambda a: 0x0200 | (a[1] << 12) | a[0]),# [V] MOV f,d
    "MOVA":  (1, lambda a: 0x1000 | a[0]),              # [V] MOVA F
    "MOVIA": (1, lambda a: 0x2400 | a[0]),              # [V]
    "MOVL":  (1, lambda a: 0x2800 | a[0]),              # [M+] andelf: 00101000 kkkkkkkk
    "MOVA1F":(1, lambda a: 0x2300 | a[0]),              # [V-WCH]
    "MOVA2F":(1, lambda a: 0x2500 | a[0]),              # [V-WCH]
    "WAITB": (1, lambda a: 0x0010 | a[0]),              # [V]
    "BCTC":  (1, lambda a: 0x001C | a[0]),              # [V]
    "BTSC":  (2, lambda a: 0x5000 | (a[1] << 8) | a[0]),# [V]
    "BTSS":  (2, lambda a: 0x5800 | (a[1] << 8) | a[0]),# [M]
    "BS":    (2, lambda a: 0x4800 | (a[1] << 8) | a[0]),# [M]
    "NOP":   (0, lambda _: 0x0000),                     # [V-IIC] LST C=0000
    "CLRA":  (0, lambda _: 0x0004),                     # [V-IIC] LST C=0004
    "ADDL":  (1, lambda a: 0x2C00 | a[0]),              # [V-IIC] LST ADDL 0xFF = 0x2CFF
    "ANDL":  (1, lambda a: 0x2900 | a[0]),              # [V-WCH]
    "XORL":  (1, lambda a: 0x2B00 | a[0]),              # [V-WCH]
    "CMPL":  (1, lambda a: 0x2F00 | a[0]),              # [V-WCH]
    "XOR":   (2, lambda a: 0x0B00 | (a[1] << 12) | a[0]),# [V-WCH]
    "CMPZ":  (2, lambda a: 0x8000 | (a[0] << 8) | (a[1] & 0xFF)),# [V-WCH]
    "BC":    (2, lambda a: 0x4000 | (a[1] << 8) | a[0]),# [V-IIC] LST BC STATUS,1 = 0x4103 (= BS without bit 11)
    "BP1F":  (2, lambda a: 0x0080 | (a[0] << 3) | a[1]),# [V-WCH]
    "BP2F":  (2, lambda a: 0x00A0 | (a[0] << 3) | a[1]),# [V-IIC] drive bit b of data onto port o: OUT1 -> 0xB8..0xBF
    "BG2F":  (2, lambda a: 0x00E0 | (a[0] << 3) | a[1]),# [V-IIC] sample port i into data bit b: IN1 -> 0xF8..0xFF
}


def load_equs(path, sym):
    for ln in open(path):
        ln = ln.split(";", 1)[0]
        m = re.match(r"\s*(\w+)\s+EQU\s+(\S+)\s*$", ln)
        if m:
            v = m.group(2)
            sym[m.group(1)] = int(v, 0) if re.match(r"0[xX]|\d", v) else sym[v]


def assemble(asm_path):
    base_dir = os.path.dirname(os.path.abspath(asm_path))
    sym = {}
    lines = open(asm_path).read().splitlines()

    # symbol pass: INCLUDEs + EQUs (so operands resolve in the encode pass)
    for ln in lines:
        l = ln.split(";", 1)[0]
        m = re.match(r"\s*INCLUDE\s+(\S+)", l, re.I)
        if m:
            load_equs(os.path.join(base_dir, m.group(1)), sym)
    for ln in lines:
        l = ln.split(";", 1)[0]
        m = re.match(r"\s*(\w+)\s+EQU\s+(\S+)\s*$", l)
        if m:
            v = m.group(2)
            sym[m.group(1)] = int(v, 0) if re.match(r"0[xX]|\d", v) else sym[v]

    # pass 1: assign addresses (ORG / labels), collect instructions
    labels, insns, addr = {}, [], 0
    for ln in lines:
        l = ln.split(";", 1)[0].rstrip()
        if not l.strip():
            continue
        if re.match(r"\s*(INCLUDE|END\b)", l, re.I):
            continue
        if re.match(r"\s*\w+\s+EQU\b", l):
            continue
        m = re.match(r"\s*ORG\s+(\S+)", l, re.I)
        if m:
            addr = int(m.group(1), 0)
            continue
        m = re.match(r"\s*(\w+):\s*(.*)$", l)        # LABEL: [instr]
        if m:
            labels[m.group(1)] = addr
            rest = m.group(2).strip()
            if not rest:
                continue
            l = "    " + rest
        parts = l.split(None, 1)
        mn = parts[0].upper()
        args = [t.strip() for t in parts[1].split(",")] if len(parts) > 1 else []
        # INC/DEC/RCL accept the WCH shorthand with an implicit F destination.
        if mn in ("INC", "DEC", "RCL") and len(args) == 1:
            args.append("F")
        insns.append((addr, mn, args))
        addr += 1                                    # every instruction / DW = 1 word

    # pass 2: encode
    def resolve(tok):
        if tok in labels:
            return labels[tok]
        if tok in sym:
            return sym[tok]
        if re.match(r"[-+]?(0[xX][0-9a-fA-F]+|0[bB][01]+|\d+)$", tok):
            return int(tok, 0)
        if tok.upper() == "A":                       # MOV F,A : the 'A' operand
            return 0
        if tok.upper() == "F":
            return 1
        raise SystemExit("error: unresolved operand '%s'" % tok)

    words = []
    current_addr = 0
    for addr, mn, args in insns:
        if addr < current_addr:
            raise SystemExit("error: ORG moves backwards before %s" % mn)
        words.extend([0] * (addr - current_addr))
        current_addr = addr
        if mn not in ENC:
            raise SystemExit(
                "error: opcode '%s' not in the verified table.\n"
                "       Confirm its C=xxxx in a verified .LST and add it to ENC." % mn)
        arity, fn = ENC[mn]
        if len(args) != arity:
            raise SystemExit("error: %s expects %d operand(s), got %d"
                             % (mn, arity, len(args)))
        words.append(fn([resolve(t) for t in args]) & 0xFFFF)
        current_addr += 1

    out = bytearray()
    for w in words:
        out += bytes((w & 0xFF, (w >> 8) & 0xFF))
    return bytes(out)


def read_inc(path):
    # Parse ONLY the C array initializer { ... }, not the comment header: a /* */ banner
    # can contain hex (e.g. "0x001E") that the 0x.. regex would otherwise read as blob bytes.
    txt = open(path).read()
    m = re.search(r"\{(.*)\}", txt, re.S)
    body = m.group(1) if m else txt
    return bytes(int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]{2}", body))


def fmt_inc(b):
    lines = []
    for i in range(0, len(b), 16):
        lines.append(",".join("0x%02X" % x for x in b[i:i + 16]))
    return "\t\t\t\t{" + (",\n\t\t\t\t ".join(lines)) + "};\n"


def main():
    if any(a in ("-h", "--help") for a in sys.argv[1:]):
        print(__doc__)
        return
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    write = "--write" in sys.argv
    here = os.path.dirname(os.path.abspath(__file__))
    asm = args[0] if args else os.path.join(here, "tapioca_swd.ASM")
    inc = re.sub(r"\.ASM$", "_inc.h", asm, flags=re.I)

    blob = assemble(asm)
    if len(blob) > 4096:
        raise SystemExit("PIOC program exceeds 2048 instruction words")
    print("%s -> %d words / %d bytes" % (os.path.basename(asm), len(blob) // 2, len(blob)))

    if write:
        open(inc, "w").write(fmt_inc(blob))
        print("wrote %s" % os.path.basename(inc))
        return
    if not os.path.exists(inc):
        raise SystemExit("no %s to check against (use --write to generate)" % inc)
    ref = read_inc(inc)
    if blob == ref:
        print("MATCH: %s matches the ASM" % os.path.basename(inc))
    else:
        print("MISMATCH vs %s (%d bytes)" % (os.path.basename(inc), len(ref)))
        for i, (x, y) in enumerate(zip(blob, ref)):
            if x != y:
                print("  byte %d: asm=0x%02X inc=0x%02X" % (i, x, y))
        sys.exit(1)


if __name__ == "__main__":
    main()
