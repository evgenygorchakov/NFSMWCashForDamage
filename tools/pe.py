"""Read-only helpers for inspecting speed.exe v1.3 next to the decomp symbol map.

Usage:
    python -I tools/pe.py vtable <va> [count]   dump vtable slots with symbol names
    python -I tools/pe.py u32 <va> [count]      dump dwords at a virtual address
    python -I tools/pe.py sym <regex>           search symbol names
    python -I tools/pe.py at <va>               which function contains <va>
"""
import bisect
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXE = Path(r"C:\Program Files (x86)\EA GAMES\Need for Speed Most Wanted\speed.exe")
SYMBOLS = ROOT / "refs" / "nfsmw" / "config" / "SPEED_EXE_1_3" / "symbols.json"


class Image:
    def __init__(self, path=EXE):
        self.data = path.read_bytes()
        pe = struct.unpack_from("<I", self.data, 0x3C)[0]
        nsec = struct.unpack_from("<H", self.data, pe + 6)[0]
        opt_size = struct.unpack_from("<H", self.data, pe + 20)[0]
        self.base = struct.unpack_from("<I", self.data, pe + 24 + 28)[0]
        self.sections = []
        sec = pe + 24 + opt_size
        for i in range(nsec):
            o = sec + 40 * i
            vsize, va, rsize, raw = struct.unpack_from("<IIII", self.data, o + 8)
            self.sections.append((self.base + va, vsize, rsize, raw))

    def offset(self, va):
        for start, vsize, rsize, raw in self.sections:
            if start <= va < start + min(vsize, rsize):
                return raw + (va - start)
        raise ValueError(f"0x{va:X} is not backed by file data")

    def u32(self, va):
        return struct.unpack_from("<I", self.data, self.offset(va))[0]

    def read(self, va, n):
        o = self.offset(va)
        return self.data[o:o + n]


class Symbols:
    def __init__(self, path=SYMBOLS):
        d = json.loads(path.read_text(encoding="utf-8"))
        self.by_addr = {}
        for x in d["names"]:
            self.by_addr.setdefault(int(x["addr"], 16), x["name"])
        self.funcs = sorted((int(f["start"], 16), int(f["size"], 16), f["name"]) for f in d["functions"])
        self.starts = [f[0] for f in self.funcs]

    def name(self, va):
        return self.by_addr.get(va, "")

    def containing(self, va):
        i = bisect.bisect_right(self.starts, va) - 1
        if i >= 0:
            start, size, name = self.funcs[i]
            if start <= va < start + size:
                return start, name
        return None, ""


def main(argv):
    cmd = argv[1]
    img, syms = Image(), Symbols()
    if cmd == "vtable":
        va, count = int(argv[2], 16), int(argv[3]) if len(argv) > 3 else 40
        print(f"vtable 0x{va:08X} {syms.name(va)}")
        for i in range(count):
            target = img.u32(va + 4 * i)
            print(f"  [{i:3d}] +0x{4 * i:03X}  0x{target:08X}  {syms.name(target)}")
    elif cmd == "u32":
        va, count = int(argv[2], 16), int(argv[3]) if len(argv) > 3 else 8
        for i in range(count):
            v = img.u32(va + 4 * i)
            print(f"  0x{va + 4 * i:08X}: 0x{v:08X}  {syms.name(v)}")
    elif cmd == "sym":
        pat = re.compile(argv[2], re.I)
        for addr, name in sorted(syms.by_addr.items()):
            if pat.search(name):
                print(f"0x{addr:08X} {name}")
    elif cmd == "at":
        start, name = syms.containing(int(argv[2], 16))
        print(f"0x{start:08X} {name}" if start is not None else "not inside a known function")


if __name__ == "__main__":
    main(sys.argv)
