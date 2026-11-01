"""Execute the linked F030 relay driver with emulated GPIO registers."""
import sys, pathlib, struct, json
if len(sys.argv)>1: sys.path.insert(0,sys.argv[1])
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0
from elftools.elf.elffile import ELFFile
root=pathlib.Path(__file__).resolve().parents[1]
with (root/'targets/stm32f030f4/build/climat_f030f4.elf').open('rb') as f:
    elf=ELFFile(f)
    segments=[(s['p_paddr'],s.data()) for s in elf.iter_segments() if s['p_type']=='PT_LOAD']
    address=next(s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols() if s.name=='relay_apply')
count=0
for previous in range(16):
    for requested in range(16):
        uc=Uc(UC_ARCH_ARM, UC_MODE_THUMB|UC_MODE_MCLASS)
        uc.mem_map(0x08000000,0x4000);uc.mem_map(0x20000000,0x1000);uc.mem_map(0x48000000,0x1000);uc.mem_map(0x1000,0x1000)
        for start,data in segments:
            if data: uc.mem_write(start,data)
        # Non-relay output bit must remain unchanged.
        uc.mem_write(0x48000014,struct.pack('<I',(previous<<1)|0x100))
        writes=[]
        def gpio(uc,access,addr,size,value,user):
            if addr==0x48000018:
                writes.append(value)
                odr=struct.unpack('<I',uc.mem_read(0x48000014,4))[0]
                uc.mem_write(0x48000014,struct.pack('<I',(odr & ~(value>>16))|(value&0xffff)))
        uc.hook_add(UC_HOOK_MEM_WRITE,gpio)
        uc.reg_write(UC_ARM_REG_SP,0x20000ff0);uc.reg_write(UC_ARM_REG_LR,0x1001);uc.reg_write(UC_ARM_REG_R0,requested)
        uc.emu_start(address|1,0x1000,count=100)
        assert writes==[((0x1e & ~(requested<<1))<<16),requested<<1],writes
        assert uc.reg_read(UC_ARM_REG_R0)==requested
        assert struct.unpack('<I',uc.mem_read(0x48000014,4))[0]==(requested<<1)|0x100
        count+=1
print(f'F030 linked relay driver: PASS ({count} transitions; OFF before ON; other GPIO preserved)')
(root/'targets/stm32f030f4/build/relay-test.json').write_text(json.dumps({'transitions':count,'passed':True,'hardware':False},indent=2))
