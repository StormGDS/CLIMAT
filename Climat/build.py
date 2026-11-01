"""Build F767 with installed ARM GCC; no device is flashed."""
import argparse, pathlib, subprocess, json, hashlib
ROOT=pathlib.Path(__file__).resolve().parent

def toolchain(value=None):
    if value: return pathlib.Path(value)
    candidates=sorted(pathlib.Path('C:/ST').glob('STM32CubeIDE_*/STM32CubeIDE/plugins/*gnu-tools*/tools/bin/arm-none-eabi-gcc.exe'))
    if not candidates: raise SystemExit('Supply --toolchain (directory containing arm-none-eabi-gcc)')
    # Known baseline, then newest installed compiler.
    candidates.sort(key=lambda p: ('9-2020' in str(p),str(p)),reverse=True)
    return candidates[0].parent

def run(args):
    # GCC on Windows may not accept Unicode absolute paths. Use project-relative paths.
    subprocess.run([str(a).replace(str(ROOT), '.') for a in args], cwd=ROOT, check=True)

def build(tc):
    dest=ROOT/'build/f767';dest.mkdir(parents=True,exist_ok=True)
    flags=['-mcpu=cortex-m7','-mthumb','-mfpu=fpv5-d16','-mfloat-abi=hard','-std=gnu11','-Os','-g3',
      '-ffunction-sections','-fdata-sections','-Wall','-Wextra','-Werror','-fstack-usage','-DUSE_HAL_DRIVER','-DSTM32F767xx']
    for path in ['Core/Inc','Drivers/STM32F7xx_HAL_Driver/Inc','Drivers/STM32F7xx_HAL_Driver/Inc/Legacy',
                 'Drivers/CMSIS/Device/ST/STM32F7xx/Include','Drivers/CMSIS/Include']:
        flags+=['-I',str(ROOT/path)]
    sources=sorted((ROOT/'Core/Src').glob('*.c'))+sorted((ROOT/'Drivers/STM32F7xx_HAL_Driver/Src').glob('*.c'))+list((ROOT/'Core/Startup').glob('*.s'))
    objects=[]
    for src in sources:
        obj=dest/(src.stem+'.o');run([tc/'arm-none-eabi-gcc.exe',*flags,*(['-Wno-unused-parameter'] if 'Drivers' in src.parts else []),'-c',src,'-o',obj]);objects.append(obj)
    elf=dest/'climat_f767.elf';binary=dest/'climat_f767.bin'
    run([tc/'arm-none-eabi-gcc.exe',*flags,'--specs=nano.specs','--specs=nosys.specs',
      '-T',ROOT/'STM32F767ZITX_FLASH.ld','-Wl,--gc-sections',f'-Wl,-Map={dest / "climat_f767.map"}',
      '-o',elf,*objects,'-Wl,--start-group','-lc','-lm','-Wl,--end-group'])
    run([tc/'arm-none-eabi-size.exe',elf]);run([tc/'arm-none-eabi-objcopy.exe','-O','binary',elf,binary])
    if binary.stat().st_size>65536: raise SystemExit('F767 code overlaps CONFIG')
    (dest/'build-report.json').write_text(json.dumps({'compiler':str(tc),'binaryBytes':binary.stat().st_size,
      'sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'flashed':False},indent=2))
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--toolchain');a=p.parse_args();build(toolchain(a.toolchain))
