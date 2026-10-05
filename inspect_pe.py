"""只读提取 PE 元数据、字符串引用与指定地址反汇编；地址为首选加载 VA。"""
import argparse
import hashlib
import json
import re
from pathlib import Path

import capstone
import pefile


def strings(pe, data):
    result = {}
    for pattern, encoding in [(rb"[\x20-\x7e]{4,}", "ascii"),
                              (rb"(?:[\x20-\x7e]\x00){4,}", "utf-16-le")]:
        for match in re.finditer(pattern, data):
            rva = pe.get_rva_from_offset(match.start())
            if rva is None:
                continue
            va = pe.OPTIONAL_HEADER.ImageBase + rva
            result[va] = match.group().decode(encoding)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("file", type=Path)
    parser.add_argument("--strings", dest="pattern")
    parser.add_argument("--refs", nargs="*", type=lambda s: int(s, 0))
    parser.add_argument("--disasm", nargs=2, type=lambda s: int(s, 0), metavar=("VA", "BYTES"))
    args = parser.parse_args()
    data = args.file.read_bytes()
    pe = pefile.PE(data=data)
    base = pe.OPTIONAL_HEADER.ImageBase
    text_strings = strings(pe, data)
    imports = {i.address: (entry.dll.decode() + "!" + (i.name.decode() if i.name else str(i.ordinal)))
               for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []) for i in entry.imports}
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32 if pe.FILE_HEADER.Machine == 0x14c else capstone.CS_MODE_64)
    md.detail = True

    def formatted(ins):
        notes = []
        for op in ins.operands:
            value = op.imm if op.type == capstone.x86.X86_OP_IMM else op.mem.disp if op.type == capstone.x86.X86_OP_MEM else None
            if value in imports:
                notes.append(imports[value])
            elif value in text_strings:
                notes.append(repr(text_strings[value][:200]))
        return f"{ins.address:08x}  {ins.mnemonic:8s} {ins.op_str:45s}" + (" ; " + " | ".join(notes) if notes else "")

    if args.disasm:
        va, size = args.disasm
        for ins in md.disasm(pe.get_data(va-base, size), va):
            print(formatted(ins))
    elif args.refs is not None:
        targets = set(args.refs)
        for section in pe.sections:
            if not section.Characteristics & 0x20000000:
                continue
            md.skipdata = True
            for ins in md.disasm(section.get_data(), base+section.VirtualAddress):
                if ins.id and any((op.type == capstone.x86.X86_OP_IMM and op.imm in targets) or
                                  (op.type == capstone.x86.X86_OP_MEM and op.mem.disp in targets)
                                  for op in ins.operands):
                    print(formatted(ins))
    elif args.pattern:
        for va, value in sorted(text_strings.items()):
            if re.search(args.pattern, value, re.I):
                print(f"{va:08x} {value}")
    else:
        print(json.dumps({"file": str(args.file.resolve()), "sha256": hashlib.sha256(data).hexdigest(),
                          "machine": hex(pe.FILE_HEADER.Machine), "image_base": hex(base),
                          "entry": hex(base+pe.OPTIONAL_HEADER.AddressOfEntryPoint),
                          "exports": [{"name": e.name.decode() if e.name else e.ordinal, "va": hex(base+e.address)}
                                      for e in getattr(pe, "DIRECTORY_ENTRY_EXPORT", type("Empty", (), {"symbols": []})()).symbols],
                          "imports": {hex(k): v for k, v in imports.items()}}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
