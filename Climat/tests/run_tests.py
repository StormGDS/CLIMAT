"""Execute the actual Cortex-M0 C tests under Unicorn; no hardware access.
Dependencies: pip install unicorn pyelftools. Pass --runtime for a local install.
"""
import sys, pathlib, argparse, json, subprocess, struct, csv
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from build import toolchain, run

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--no-console',action='store_true');ap.add_argument('--runtime');ap.add_argument('--toolchain');ap.add_argument('--compact',action='store_true');ap.add_argument('--production',action='store_true');args=ap.parse_args()
    if args.runtime: sys.path.insert(0,str(pathlib.Path(args.runtime).resolve()))
    from elftools.elf.elffile import ELFFile
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
    from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_PC
    tc=toolchain(args.toolchain);dest=ROOT/('build/tests-no-console' if args.no_console else 'build/tests-production' if args.production else 'build/tests-compact' if args.compact else 'build/tests');dest.mkdir(parents=True,exist_ok=True)
    flags=['-mcpu=cortex-m0','-mthumb','-std=c11','-Os','-g','-Wall','-Wextra','-Werror','-I',str(ROOT/'Core/Inc')]
    if args.no_console: flags+=['-DCLIMAT_NO_SERVICE_CONSOLE']
    if args.compact or args.production: flags+=['-DCLIMAT_COMPACT']
    if args.production: flags[flags.index('-Os')]='-Oz';flags+=['-DCLIMAT_PRODUCTION']
    names=['auto_control','auto_selftest','control_core','control_selftest','settings','settings_selftest',
      'phone_link','phone_selftest','bt_boot','bt_boot_selftest','tiny_format','tiny_format_selftest','service_console']
    sources=[ROOT/'Core/Src'/f'{name}.c' for name in names]+[ROOT/'tests/test_temperature_probe.c',ROOT/'tests/test_service.c',ROOT/'tests/test_vectors.c',ROOT/'tests/test_rate_control.c']
    objects=[]
    for src in sources:
        obj=dest/(src.stem+'.o');run([tc/'arm-none-eabi-gcc.exe',*flags,'-c',src,'-o',obj]);objects.append(obj)
    elf=dest/'tests.elf';run([tc/'arm-none-eabi-gcc.exe',*flags,'-nostartfiles','--specs=nano.specs','--specs=nosys.specs',
      '-T',ROOT/'tests/emulator.ld','-o',elf,*objects,'-lc','-lgcc'])
    with elf.open('rb') as f:
        ef=ELFFile(f);segments=[(s['p_vaddr'],s.data()) for s in ef.iter_segments() if s['p_type']=='PT_LOAD']
        symbols={s.name:s['st_value'] for s in ef.get_section_by_name('.symtab').iter_symbols()}
    functions=['auto_selftest','control_selftest','settings_selftest','probe_trend_selftest','probe_test','bindings_test',
               'phone_selftest','bt_boot_selftest','tiny_format_selftest','service_selftest','vectors_selftest','service_rate_test','rate_boundaries_test','rate_timing_test','rate_integration_test','parameter_sensitivity_test']
    results={}
    for name in functions:
        uc=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS);uc.mem_map(0x10000,0x100000);uc.mem_map(0x20000000,0x20000);uc.mem_map(0x2000,0x1000)
        for addr,data in segments:
            if data: uc.mem_write(addr,data)
        uc.reg_write(UC_ARM_REG_SP,0x2001fff0);uc.reg_write(UC_ARM_REG_LR,0x2001)
        uc.emu_start(symbols[name]|1,0x2000,timeout=30000000,count=30000000)
        if uc.reg_read(UC_ARM_REG_PC)!=0x2000: raise RuntimeError(f'{name}: did not return')
        if name in ['rate_timing_test','rate_integration_test','parameter_sensitivity_test']:
            count=struct.unpack('<I',uc.mem_read(symbols['rate_trace_count'],4))[0]
            rows=[struct.unpack('<IIiiII',uc.mem_read(symbols['rate_trace']+24*i,24)) for i in range(count)]
            with (dest/(name+'.csv')).open('w',newline='') as trace:
                writer=csv.writer(trace);writer.writerow(['scenario','time_ms','raw16','rate_mC_min','requested','logical']);writer.writerows(rows)
        result=uc.reg_read(UC_ARM_REG_R0);results[name]=result
        print(f'{name}: {"PASS" if result==0 else "FAIL"} (0x{result:X})',flush=True)
    (dest/'results.json').write_text(json.dumps({'cpu':'emulated Cortex-M0','compact':args.compact or args.production,'production':args.production,'no_console':args.no_console,'compiler':str(tc),'results':results},indent=2))
    if any(results.values()): raise SystemExit(1)
if __name__=='__main__': main()
