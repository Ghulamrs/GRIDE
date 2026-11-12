<#
  probe.ps1 - one of the runtime's UCRT probes (docs\Runtime_Design.md 11.2): a C source (c90) or a C++ one
  (cpp11) compiled once, linked with GRIDE's runtime alone (LIB empty, the six libraries by path) and, where Visual
  Studio's and the SDK's libraries are found, with Microsoft's static CRT as GRIDE links it; each run, and its
  stdout, stderr and exit code compared with the recorded transcript beside the source (<name>.expected), which the
  UCRT's run wrote. A line that says "N wrong" must say 0 wrong. -Record writes the transcript from the UCRT's run.
  -OwnConsole runs each program in a hidden console of its own, for a probe that sends its console Ctrl+C, which
  reaches every process the console has. -DebugRuntime links the Debug variants: GRIDE's grtstartd ... grtwind and
  Microsoft's libcmtd, libucrtd and libvcruntimed; the transcript (<name>.debug.expected where there is one) is
  what a correct program prints through either. No /subsystem is given: link takes it, and the entry, from what
  the object defines, so a source with WinMain is a windows program here.

    .\probe.ps1 <source> [-Opt -O2|-O0] [-Bin <toolchain folder>] [-Libs <the runtime's folder>] [-Record] [-OwnConsole]
                [-DebugRuntime]
#>
param([Parameter(Mandatory = $true)][string]$Source, [string]$Opt = '-O2', [string]$Bin = "", [string]$Libs = "",
      [switch]$Record, [switch]$OwnConsole, [switch]$DebugRuntime)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
if (-not $Bin) { $Bin = (Resolve-Path (Join-Path $here '..\..\..\..\GRIDEd\x64\Release')).Path }
if (-not $Libs) { $Libs = Join-Path $Bin 'lib\x86_64-windows'; if (-not (Test-Path (Join-Path $Libs 'grtc.lib'))) { $Libs = Join-Path $Bin 'lib' } }
$Source = (Resolve-Path $Source).Path
$name = [IO.Path]::GetFileNameWithoutExtension($Source)
$work = Join-Path $env:TEMP "gride-probe-$name"

function Tool([string]$tool, [string[]]$a) {
    $o = & (Join-Path $Bin "$tool.exe") @a 2>&1
    if ($LASTEXITCODE -ne 0) { throw "$tool failed:`n$($o -join "`n")" }
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

if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory $work | Out-Null
$compiler = if ([IO.Path]::GetExtension($Source) -eq '.c') { 'c90' } else { 'cpp11' }
Tool $compiler @('-S', $Source, '-o', "$work\$name.s", '-arch', 'x86_64-windows', $Opt, '-masm=masm', '-nologo')
Tool masm @('-t', 'x64', "$work\$name.s", '-o', "$work\$name.obj")
$d = if ($DebugRuntime) { 'd' } else { '' }
$grt = @(@('grtstart', 'grtcompat', 'grtcxx', 'grtc', 'grtwin') | ForEach-Object { Join-Path $Libs "$_$d.lib" }) + (Join-Path $Libs 'kernel32.lib')
$saved = $env:LIB
try {
    $env:LIB = ''
    # recording needs only the UCRT's run: a runtime still without the probe's names is reported, not fatal
    $grtLinked = $true
    try { Tool link (@('/nologo', "/out:$work\$name-grt.exe", "$work\$name.obj") + $grt) }
    catch { if (-not $Record) { throw }; $grtLinked = $false; "${name} grt: not linked - $_" }
    $ms = MicrosoftLib
    if ($ms) {
        $env:LIB = $ms
        Tool link @('/nologo', "/out:$work\$name-msvc.exe", "$work\$name.obj", "libcmt$d.lib", "libucrt$d.lib",
                    "libvcruntime$d.lib", 'kernel32.lib', 'legacy_stdio_definitions.lib', 'oldnames.lib')
    }
} finally { $env:LIB = $saved }

$results = [ordered]@{}
foreach ($k in 'grt', 'msvc') {
    if ($k -eq 'msvc' -and -not $ms) { continue }
    if ($k -eq 'grt' -and -not $grtLinked) { continue }
    $t = [Diagnostics.Stopwatch]::StartNew()
    $console = if ($OwnConsole) { @{ WindowStyle = 'Hidden' } } else { @{ NoNewWindow = $true } }
    $p = Start-Process -FilePath "$work\$name-$k.exe" -WorkingDirectory $work @console -Wait -PassThru `
                       -RedirectStandardOutput "$work\$k.out" -RedirectStandardError "$work\$k.err"
    $results[$k] = ((@(Get-Content "$work\$k.out") + @(Get-Content "$work\$k.err" | ForEach-Object { "stderr: $_" }) + "exit $($p.ExitCode)") -join "`n") + "`n"
    "{0} {1}: {2:N1} s" -f $name, $k, $t.Elapsed.TotalSeconds
}
$expectedFile = [IO.Path]::ChangeExtension($Source, '.expected')
$debugExpected = [IO.Path]::ChangeExtension($Source, '.debug.expected')
if ($DebugRuntime -and ($Record -or (Test-Path $debugExpected))) { $expectedFile = $debugExpected }
if ($Record) {
    if (-not $ms) { throw "${name}: -Record needs Microsoft's CRT, and no Visual Studio was found" }
    [IO.File]::WriteAllText($expectedFile, $results['msvc'])
    "$name.expected recorded from the UCRT's run"
}
$expected = [IO.File]::ReadAllText($expectedFile).Replace("`r`n", "`n")
$faults = 0
foreach ($k in $results.Keys) {
    $bad = @($results[$k] -split "`n" | Where-Object { $_ -match ' wrong' -and $_ -notmatch ' 0 wrong' })
    if ($results[$k] -eq $expected -and $bad.Count -eq 0) { "${name} ${k}: the same as $name.expected" }
    else {
        $faults++
        "${name} ${k}: differs from $name.expected or has wrong answers (output: $work\$k.out)"
        $e = $expected -split "`n"
        $g = $results[$k] -split "`n"
        $shown = 0
        for ($i = 0; $i -lt [Math]::Max($e.Count, $g.Count) -and $shown -lt 20; $i++) {
            if ($e[$i] -ne $g[$i]) { "  line $($i + 1): expected '$($e[$i])'"; "  line $($i + 1): got      '$($g[$i])'"; $shown++ }
        }
    }
}
if (-not $ms) { "${name} msvc: not run (no Visual Studio here); grt compared with the recorded UCRT transcript alone" }
if ($faults) { throw "${name}: $faults run(s) differ" }
