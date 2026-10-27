<#
  threads.ps1 - the locks' probe (docs\Runtime_Design.md 9.6): probe.cpp compiled once by cpp11, linked with GRIDE's
  runtime alone (LIB empty, the six libraries by path) and, where Visual Studio's and the SDK's libraries are found,
  with Microsoft's static CRT as GRIDE links it. GRIDE's is run -Runs times, Microsoft's once, each in a folder of its
  own; every transcript (stdout, stderr, the exit code) is compared with threads.expected, which the UCRT's run
  recorded. A runtime whose tables are not locked crashes here or loses lines. -Record writes threads.expected from
  the UCRT's run.

    .\threads.ps1 [-Bin <toolchain folder>] [-Libs <the runtime's folder>] [-Runs 30] [-Record]
#>
param([string]$Bin = "", [string]$Libs = "", [int]$Runs = 30, [switch]$Record)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
if (-not $Bin) { $Bin = (Resolve-Path (Join-Path $here '..\..\..\..\..\GRIDEd\x64\Release')).Path }
if (-not $Libs) { $Libs = Join-Path $Bin 'lib' }
$work = Join-Path $env:TEMP 'gride-threads'

function Tool([string]$name, [string[]]$a) {
    $o = & (Join-Path $Bin "$name.exe") @a 2>&1
    if ($LASTEXITCODE -ne 0) { throw "$name failed:`n$($o -join "`n")" }
}

# Microsoft's static CRT where Visual Studio and the SDK are installed: their library folders, or $null
function MicrosoftLib {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $kit = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Lib'
    if (-not (Test-Path $vswhere) -or -not (Test-Path $kit)) { return $null }
    $vs = & $vswhere -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
    if (-not $vs) { return $null }
    $tools = Get-ChildItem (Join-Path $vs 'VC\Tools\MSVC') -Directory | Where-Object { $_.Name -match '^\d+(\.\d+)+$' } |
             Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    $sdk = Get-ChildItem $kit -Directory | Where-Object { $_.Name -match '^\d+(\.\d+)+$' -and (Test-Path (Join-Path $_.FullName 'ucrt\x64\libucrt.lib')) } |
           Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    if (-not $tools -or -not $sdk) { return $null }
    return @((Join-Path $tools.FullName 'lib\x64'), (Join-Path $sdk.FullName 'ucrt\x64'), (Join-Path $sdk.FullName 'um\x64')) -join ';'
}

# the probe run once in a folder of its own: its lines, its stderr's marked, and its exit code
function Transcript([string]$exe, [string]$tag) {
    $folder = Join-Path $work $tag
    New-Item -ItemType Directory -Force $folder | Out-Null
    $p = Start-Process -FilePath $exe -WorkingDirectory $folder -NoNewWindow -Wait -PassThru `
                       -RedirectStandardOutput "$work\run.out" -RedirectStandardError "$work\run.err"
    $lines = @(Get-Content "$work\run.out") + @(Get-Content "$work\run.err" | ForEach-Object { "stderr: $_" })
    Remove-Item $folder -Recurse -Force
    return (($lines + ('exit 0x{0:X8}' -f $p.ExitCode)) -join "`n") + "`n"
}

if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory $work | Out-Null
Tool cpp11 @('-S', "$here\probe.cpp", '-o', "$work\probe.s", '-arch', 'x86_64-windows', '-O2', '-masm=masm', '-nologo')
Tool masm @('-t', 'x64', "$work\probe.s", '-o', "$work\probe.obj")
$grt = @('grtstart', 'grtcompat', 'grtcxx', 'grtc', 'grtwin', 'kernel32') | ForEach-Object { Join-Path $Libs "$_.lib" }
$saved = $env:LIB
try {
    $env:LIB = ''
    Tool link (@('/nologo', '/subsystem:console', "/out:$work\probe-grt.exe", "$work\probe.obj") + $grt)
    $ms = MicrosoftLib
    if ($ms) {
        $env:LIB = $ms
        Tool link @('/nologo', '/subsystem:console', "/out:$work\probe-msvc.exe", "$work\probe.obj", 'libcmt.lib', 'libucrt.lib',
                    'libvcruntime.lib', 'kernel32.lib', 'legacy_stdio_definitions.lib')
    }
} finally { $env:LIB = $saved }

$expectedFile = Join-Path $here 'threads.expected'
if ($ms) {
    $m = Transcript "$work\probe-msvc.exe" 'msvc'
    if ($Record) { [IO.File]::WriteAllText($expectedFile, $m); "threads.expected recorded from the UCRT's run" }
} elseif ($Record) { throw 'threads: -Record needs Microsoft''s CRT, and no Visual Studio was found' }
$expected = [IO.File]::ReadAllText($expectedFile).Replace("`r`n", "`n")
if ($ms) {
    if ($m -eq $expected) { 'msvc: the same as threads.expected' } else { [IO.File]::WriteAllText("$work\msvc.txt", $m); throw "threads: the UCRT's run differs from threads.expected ($work\msvc.txt)" }
} else { 'msvc: not run (no Visual Studio here)' }
$faults = 0
for ($i = 1; $i -le $Runs; $i++) {
    $g = Transcript "$work\probe-grt.exe" "grt-$i"
    if ($g -ne $expected) {
        $faults++
        [IO.File]::WriteAllText("$work\grt-$i.txt", $g)
        "grt run ${i}: differs from threads.expected (transcript: $work\grt-$i.txt)"
    }
}
"grt: $($Runs - $faults) of $Runs runs the same as threads.expected"
if ($faults) { throw "threads: $faults run(s) differ" }
