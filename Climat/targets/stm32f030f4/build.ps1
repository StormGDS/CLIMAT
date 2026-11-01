param(
  [string]$CubeF0Path=(Join-Path $env:USERPROFILE 'STM32Cube\Repository\STM32Cube_FW_F0_V1.11.5'),
  [string]$ToolchainPath='C:\ST\STM32CubeIDE_1.15.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.12.3.rel1.win32_1.0.100.202403111256\tools\bin',
  [string]$Optimize='-Oz',
  [switch]$NoLto
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$build=Join-Path $PSScriptRoot $(if($NoLto){'build-no-lto'}else{'build'})
New-Item -ItemType Directory -Force -Path $build|Out-Null
$gcc=Join-Path $ToolchainPath 'arm-none-eabi-gcc.exe'
if(!(Test-Path -LiteralPath $gcc)){throw 'Supply -ToolchainPath for ARM GCC'}
$device=Join-Path $CubeF0Path 'Drivers\CMSIS\Device\ST\STM32F0xx\Include'
$cmsis=Join-Path $CubeF0Path 'Drivers\CMSIS\Include'
if(!(Test-Path -LiteralPath (Join-Path $device 'stm32f030x6.h'))){throw 'Supply -CubeF0Path for installed STM32CubeF0'}
$flags=@('-mcpu=cortex-m0','-mthumb','-std=gnu11',$Optimize,'-g3','-Wall','-Wextra','-Werror',
  '-ffunction-sections','-fdata-sections','-fno-common','-fno-unwind-tables','-fno-asynchronous-unwind-tables',
  '-fstack-usage','-DSTM32F030x6','-DCLIMAT_COMPACT','-DCLIMAT_PRODUCTION',
  '-I',(Join-Path $root 'Core\Inc'),'-I',$PSScriptRoot,'-I',$device,'-I',$cmsis)
if(!$NoLto){$flags+='-flto'}
$sources=@('auto_control','control_core','temperature_probe','settings','phone_link','bt_boot','service_console','tiny_format')|
  ForEach-Object {Join-Path $root "Core\Src\$_.c"}
$sources+=@((Join-Path $PSScriptRoot 'startup.c'),(Join-Path $PSScriptRoot 'board.c'))
$objects=foreach($source in $sources){
  $object=Join-Path $build ([IO.Path]::GetFileNameWithoutExtension($source)+'.o')
  & $gcc @flags -c $source -o $object
  if($LASTEXITCODE){throw "Compile failed: $source"}
  $object
}
$elf=Join-Path $build 'climat_f030f4.elf'
$link=@('-mcpu=cortex-m0','-mthumb',$Optimize,'-nostartfiles','--specs=nano.specs','--specs=nosys.specs',
  '-T',(Join-Path $PSScriptRoot 'stm32f030f4.ld'),'-Wl,--gc-sections','-Wl,--cref','-Wl,--print-memory-usage',
  ('-Wl,-Map='+ (Join-Path $build 'climat_f030f4.map')))
if(!$NoLto){$link+=@('-flto','-fstack-usage')}
Push-Location -LiteralPath $build
try {
  & $gcc @link @objects -lc -lgcc -o $elf
  if($LASTEXITCODE){throw 'Link failed (memory ASSERTs enforce 15K/4K)'}
} finally {Pop-Location}
& (Join-Path $ToolchainPath 'arm-none-eabi-size.exe') $elf
if($LASTEXITCODE){throw 'Size check failed'}
& (Join-Path $ToolchainPath 'arm-none-eabi-objcopy.exe') -O binary $elf (Join-Path $build 'climat_f030f4.bin')
if($LASTEXITCODE){throw 'Binary generation failed'}
if((Get-Item (Join-Path $build 'climat_f030f4.bin')).Length -gt 15360){throw 'BIN exceeds 15360 bytes'}
& (Join-Path $ToolchainPath 'arm-none-eabi-nm.exe') -S --size-sort $elf|Set-Content (Join-Path $build 'symbols.txt')
& (Join-Path $ToolchainPath 'arm-none-eabi-objdump.exe') -h -d $elf|Set-Content (Join-Path $build 'disassembly.txt')
& (Join-Path $PSScriptRoot 'verify.ps1') -BuildPath $build -ToolchainPath $ToolchainPath
'F030F4 build completed. Approved pin map; relays active HIGH.'
