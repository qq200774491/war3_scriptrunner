"""离线检查候选 DLL；不加载、不运行候选文件，也不自动修改白名单。"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

import capstone
from capstone.x86 import X86_OP_IMM, X86_OP_MEM
import pefile

PROFILE = json.loads((Path(__file__).resolve().parents[1] / 'compatibility/engine-v1.json').read_text(encoding='utf-8'))
VERIFIED_HASH = PROFILE['sha256']
CREATE = PROFILE['create_export']
DESTROY = PROFILE['destroy_export']


def inspect(path):
    data = path.read_bytes()
    pe = pefile.PE(data=data)
    base = pe.OPTIONAL_HEADER.ImageBase
    exports = {entry.name.decode(): entry.address for entry in getattr(
        pe, 'DIRECTORY_ENTRY_EXPORT', type('Empty', (), {'symbols': []})()).symbols if entry.name}
    result = {'file': str(path.resolve()), 'sha256': hashlib.sha256(data).hexdigest(),
              'x86': pe.FILE_HEADER.Machine == 0x14c,
              'required_exports': {name: name in exports for name in (CREATE, DESTROY)},
              'runner_vtables': []}
    result['verified_version'] = result['sha256'] == VERIFIED_HASH
    if not result['x86'] or CREATE not in exports:
        return result
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True

    def instructions(rva, size):
        return md.disasm(pe.get_data(rva, size), base + rva)

    def executable(va):
        section = pe.get_section_by_rva(va - base)
        return section is not None and bool(section.Characteristics & 0x20000000)

    # 工厂直接调用构造函数；从构造函数写入的虚表指针恢复方法地址。
    candidates = set()
    for ins in instructions(exports[CREATE], 0x100):
        if ins.mnemonic == 'ret':
            break
        if ins.mnemonic == 'call' and ins.operands[0].type == X86_OP_IMM:
            target = ins.operands[0].imm
            if not executable(target):
                continue
            for item in instructions(target - base, 0x300):
                if item.mnemonic == 'ret':
                    break
                if (item.mnemonic != 'mov' or len(item.operands) != 2 or
                        item.operands[0].type != X86_OP_MEM or item.operands[0].mem.disp != 0 or
                        item.operands[1].type != X86_OP_IMM):
                    continue
                address = item.operands[1].imm
                section = pe.get_section_by_rva(address - base)
                if section is None or section.Characteristics & 0x20000000:
                    continue
                raw = pe.get_data(address - base, 20)
                if len(raw) == 20 and all(executable(v) for v in struct.unpack('<5I', raw)):
                    candidates.add(address)
    for address in sorted(candidates):
        methods = struct.unpack('<5I', pe.get_data(address - base, 20))
        shapes = []
        for method in methods:
            # 只观察首个返回指令；不把后续函数混入当前方法，也不冒充完整 CFG。
            cleanup = next((ins.op_str or '0' for ins in instructions(method - base, 0x400)
                            if ins.mnemonic == 'ret'), None)
            shapes.append({'rva': hex(method - base), 'first_linear_ret_cleanup': cleanup})
        result['runner_vtables'].append({'rva': hex(address - base), 'methods': shapes})
    return result


def report(candidate, baseline):
    new = inspect(candidate)
    old = inspect(baseline)
    new['same_file_as_baseline'] = new['sha256'] == old['sha256']
    new['baseline_vtables'] = old['runner_vtables']
    new['can_run_with_current_host'] = new['verified_version']
    new['decision'] = ('Verified exact build; current host accepts it.' if new['verified_version'] else
                       'Unknown build: current host refuses execution. Review ABI and run isolated regression before approval.')
    new['scope'] = 'Static structural hints only; export names and vtable recovery do not prove binary compatibility.'
    return new


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('candidate', type=Path)
    parser.add_argument('--baseline', type=Path, default=Path(__file__).resolve().parents[1] / 'scriptrunner.dll')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = report(args.candidate, args.baseline)
    content = json.dumps(result, indent=2, ensure_ascii=False)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(content, encoding='utf-8')
    else:
        print(content)
