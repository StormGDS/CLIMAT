param(
  [string]$BuildPath=(Join-Path $PSScriptRoot 'build'),
  [string]$ToolchainPath='C:\ST\STM32CubeIDE_1.7.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.9-2020-q2-update.win32_2.0.0.202105311346\tools\bin'
)
$ErrorActionPreference='Stop'
$elf=Join-Path $BuildPath 'climat_f030f4.elf'
$bin=Join-Path $BuildPath 'climat_f030f4.bin'
$symbols=@{}
& (Join-Path $ToolchainPath 'arm-none-eabi-nm.exe') -n $elf | ForEach-Object {
  if($_ -match '^([0-9a-fA-F]+)\s+\S\s+(\S+)$') {$symbols[$Matches[2]]=[Convert]::ToUInt32($Matches[1],16)}
}
if($LASTEXITCODE){throw 'Cannot read ELF symbols'}
foreach($name in @('_sdata','_edata','_sbss','_end','_stack_limit','_estack','__config_start__','__config_end__','Reset_Handler')) {
  if(!$symbols.ContainsKey($name)){throw "Missing symbol: $name"}
}
if($symbols['__config_start__'] -ne 0x08003C00 -or $symbols['__config_end__'] -ne 0x08004000){throw 'Incorrect settings page'}
if($symbols['_sdata'] -ne 0x20000000 -or $symbols['_estack'] -ne 0x20001000){throw 'Incorrect RAM layout'}
$stack=$symbols['_estack']-$symbols['_stack_limit']
$globals=$symbols['_end']-0x20000000
if($stack -ne 2048 -or $globals+$stack -gt 4096){throw 'RAM budget exceeded'}
$bytes=[IO.File]::ReadAllBytes($bin)
if($bytes.Length -gt 15360 -or $bytes.Length -lt 192){throw 'Flash budget/vector table invalid'}
if([BitConverter]::ToUInt32($bytes,0) -ne $symbols['_estack']){throw 'Initial SP invalid'}
if([BitConverter]::ToUInt32($bytes,4) -ne ($symbols['Reset_Handler'] -bor 1)){throw 'Reset vector invalid'}
if([BitConverter]::ToUInt32($bytes,43*4) -ne ($symbols['USART1_IRQHandler'] -bor 1)){throw 'USART1 vector invalid'}
$sections=@(& (Join-Path $ToolchainPath 'arm-none-eabi-objdump.exe') -h $elf)
if($LASTEXITCODE){throw 'Cannot read ELF sections'}
for($i=0;$i -lt $sections.Length-1;++$i) {
  if($sections[$i] -match '^\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+' -and $sections[$i+1] -match '\bLOAD\b') {
    # Re-match below: a second -match resets $Matches.
    [void]($sections[$i] -match '^\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+')
    $name=$Matches[1];$size=[Convert]::ToUInt32($Matches[2],16);$load=[Convert]::ToUInt32($Matches[4],16)
    if($load -lt 0x08000000 -or $load+$size -gt 0x08003C00){throw "Load section overlaps settings/outside program: $name"}
  }
}
if($symbols.Keys -match '^(malloc|calloc|realloc|free|_sbrk|snprintf|strtol|__aeabi_[ul]*ldivmod|__divdi3|__udivdi3)$'){throw 'Unexpected heap/large runtime helper'}
$ramCode=@(& (Join-Path $ToolchainPath 'arm-none-eabi-objdump.exe') -d -j .data $elf)
if($LASTEXITCODE -or !($ramCode -match '<flash_program') -or ($ramCode -match '\sblx?\s')){throw 'RAM flash writer missing or contains a call'}
if($symbols.Keys -match '^(command|service_rx_byte|service_rx_error|service_stream_enabled|service_set_bt_handler|report|console|bt_status)$'){throw 'Service console unexpectedly linked'}
$report=[ordered]@{
  serviceConsoleEnabled=$false; pa5Used=$false
  target='STM32F030F4P6'; boardRoutingApproved=$true; physicalRelaysLocked=$false
  programFlashBytes=$bytes.Length; programFlashLimit=15360; programFlashFree=15360-$bytes.Length
  settingsPageStart='0x08003C00'; settingsPageBytes=1024; settingsPageInImage=$false
  ramDataCodeAndBssBytes=$globals; stackReservedBytes=$stack; ramBudgetUsed=$globals+$stack
  ramLimit=4096; ramFreeGap=4096-$globals-$stack
  stackHighWaterMeasuredOnF030=$false
  checks=@('linker ASSERTs','load sections','vectors','RAM flash writer has no calls','no heap/large runtime helpers')
  sha256=(Get-FileHash -LiteralPath $bin -Algorithm SHA256).Hash
}
$report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $BuildPath 'memory-report.json') -Encoding utf8
$report | ConvertTo-Json -Depth 4
