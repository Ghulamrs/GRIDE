<#
  debug.ps1 - the Debug variants (docs\Runtime_Design.md 4.3): debugheap.c through probe.ps1 -DebugRuntime against
  the Debug UCRT's transcript, then ends.c on GRIDE's Debug runtime alone, since Microsoft's shows a dialog there:
  abort writes "abort() has been called" and ends with exit code 3; a free that finds a block's guard overwritten
  writes the Debug UCRT's report and ends with exit code 3.

    .\debug.ps1 [-Bin <toolchain folder>] [-Libs <the runtime's folder>]
#>
param([string]$Bin = "", [string]$Libs = "")
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
if (-not $Bin) { $Bin = (Resolve-Path (Join-Path $here '..\..\..\..\..\GRIDEd\x64\Release')).Path }
if (-not $Libs) { $Libs = Join-Path $Bin 'lib' }
foreach ($o in '-O0', '-O2') { & (Join-Path $here '..\probe.ps1') (Join-Path $here 'debugheap.c') -Opt $o -Bin $Bin -Libs $Libs -DebugRuntime }

$work = Join-Path $env:TEMP 'gride-debug-ends'
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory $work | Out-Null
& "$Bin\c90.exe" -S "$here\ends.c" -o "$work\ends.s" -arch x86_64-windows -O0 -g -masm=masm -nologo
& "$Bin\masm.exe" -t x64 "$work\ends.s" -o "$work\ends.obj" | Out-Null
$saved = $env:LIB
$env:LIB = ''
& "$Bin\link.exe" /nologo /subsystem:console "/out:$work\ends.exe" "$work\ends.obj" (@('grtstartd', 'grtcompatd', 'grtcxxd', 'grtcd', 'grtwind', 'kernel32') | ForEach-Object { Join-Path $Libs "$_.lib" })
$env:LIB = $saved
$faults = 0
$want = @{
    abort   = "start`nabort() has been called`nexit 3"
    overrun = "start`nHEAP CORRUPTION DETECTED: after Normal block at 0x*`nCRT detected that the application wrote to memory after end of heap buffer.`nexit 3"
}
foreach ($case in 'abort', 'overrun') {
    $p = Start-Process "$work\ends.exe" -ArgumentList $case -NoNewWindow -Wait -PassThru -RedirectStandardError "$work\$case.err" -RedirectStandardOutput "$work\$case.out"
    $got = ((@(Get-Content "$work\$case.err") + "exit $($p.ExitCode)") -join "`n")
    if ($got -like $want[$case]) { "ends $case`: as the Debug runtime ends" }
    else { $faults++; "ends $case`: differs"; $got }
}
if ($faults) { throw "debug: $faults case(s) differ" }
