param(
  [string]$Runtime='',
  [string]$Toolchain12='C:\ST\STM32CubeIDE_1.15.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.12.3.rel1.win32_1.0.100.202403111256\tools\bin'
)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$extra=@();if($Runtime){$extra=@('--runtime',$Runtime)}
foreach($mode in @('normal','compact','production')) {
  $arguments=@((Join-Path $PSScriptRoot 'run_tests.py'))+$extra
  if($mode -eq 'compact'){$arguments+='--compact'}
  if($mode -eq 'production'){$arguments+=@('--production','--toolchain',$Toolchain12)}
  & python @arguments
  if($LASTEXITCODE){throw "C tests failed: $mode"}
}
. (Join-Path $PSScriptRoot 'phone_codec.ps1')
$cases=Get-Content (Join-Path $PSScriptRoot 'phone_vectors.json') -Raw | ConvertFrom-Json
foreach($v in $cases){
  $wire=New-PhoneFrame -Op $v.opcode -Sequence $v.sequence -Payload ([Convert]::FromHexString($v.payload_hex))
  if([Convert]::ToHexString($wire) -ne $v.wire_hex){throw "Vector mismatch: $($v.name)"}
  if((Read-PhoneFrame $wire).Version -ne 3){throw 'Incorrect protocol version'}
}
"PASS: $($cases.Count) independent PowerShell vectors"
& python (Join-Path $project 'build.py')
if($LASTEXITCODE){throw 'F767 build failed'}
& (Join-Path $project 'targets/stm32f030f4/build.ps1') -ToolchainPath $Toolchain12
if($LASTEXITCODE){throw 'F030 build failed'}
'PASS: offline C tests, protocol vectors, F767 and F030 builds. No hardware access.'
