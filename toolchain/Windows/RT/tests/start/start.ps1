<#
  start.ps1 - the entries (docs\Runtime_Design.md 7): each entry's probe through probe.ps1, c90's and cpp11's, at -O2
  and -O0 and over the Debug variants, then link's choice for every set of the four names against linked.expected,
  which is what Microsoft's link.exe does with them. A row of that file is the names the objects define, the /subsystem
  given (- for none), and the result: the subsystem and the function that ran, or "undefined" and the name link asked
  for. -Record writes it again from Microsoft's link.exe, where there is one.

    .\start.ps1 [-Bin <toolchain folder>] [-Libs <the runtime's folder>] [-Record]
#>
param([string]$Bin = "", [string]$Libs = "", [switch]$Record)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
if (-not $Bin) { $Bin = (Resolve-Path (Join-Path $here '..\..\..\..\..\GRIDEd\x64\Release')).Path }
if (-not $Libs) { $Libs = Join-Path $Bin 'lib\x86_64-windows'; if (-not (Test-Path (Join-Path $Libs 'grtc.lib'))) { $Libs = Join-Path $Bin 'lib' } }

if (-not $Record) {
    foreach ($source in 'wmainc.c', 'wmain.cpp', 'winmainc.c', 'winmain.cpp', 'wwinmainc.c', 'wwinmain.cpp') {
        $probe = Join-Path $here '..\probe.ps1'
        & $probe (Join-Path $here $source) -Opt -O2 -Bin $Bin -Libs $Libs
        & $probe (Join-Path $here $source) -Opt -O0 -Bin $Bin -Libs $Libs
        & $probe (Join-Path $here $source) -Opt -O0 -Bin $Bin -Libs $Libs -DebugRuntime
    }
}

$work = Join-Path $env:TEMP 'gride-start-entries'
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory $work | Out-Null
function Tool([string]$tool, [string[]]$a) {
    $o = & (Join-Path $Bin "$tool.exe") @a 2>&1
    if ($LASTEXITCODE -ne 0) { throw "$tool failed:`n$($o -join "`n")" }
}
$names = 'main', 'wmain', 'WinMain', 'wWinMain'
for ($i = 0; $i -lt $names.Count; $i++) {
    $n = $names[$i]
    [IO.File]::WriteAllText("$work\$n.c", "int $n(void) { return $($i + 1); }`n")
    Tool c90 @('-S', "$work\$n.c", '-o', "$work\$n.s", '-arch', 'x86_64-windows', '-O2', '-masm=masm', '-nologo')
    Tool masm @('-t', 'x64', "$work\$n.s", '-o', "$work\$n.obj")
}
$sets = @('main'), @('wmain'), @('WinMain'), @('wWinMain'), @('main', 'wmain'), @('main', 'WinMain'), @('main', 'wWinMain'),
        @('wmain', 'WinMain'), @('wmain', 'wWinMain'), @('WinMain', 'wWinMain'), @('main', 'wmain', 'WinMain'),
        @('main', 'wmain', 'WinMain', 'wWinMain')

if ($Record) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vs = & $vswhere -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
    $tools = Get-ChildItem (Join-Path $vs 'VC\Tools\MSVC') -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    $kit = Get-ChildItem (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Lib') -Directory |
           Where-Object { Test-Path (Join-Path $_.FullName 'ucrt\x64\libucrt.lib') } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    $env:LIB = @((Join-Path $tools.FullName 'lib\x64'), (Join-Path $kit.FullName 'ucrt\x64'), (Join-Path $kit.FullName 'um\x64')) -join ';'
    $linker = Join-Path $tools.FullName 'bin\Hostx64\x64\link.exe'
    $libraries = 'libcmt.lib', 'libucrt.lib', 'libvcruntime.lib', 'kernel32.lib'
} else {
    $saved = $env:LIB
    $env:LIB = ''
    $linker = Join-Path $Bin 'link.exe'
    $libraries = @('grtstart', 'grtcompat', 'grtcxx', 'grtc', 'grtwin', 'kernel32') | ForEach-Object { Join-Path $Libs "$_.lib" }
}

# the result of one link and run, as a row of linked.expected has it
function Linked([string[]]$set, [string]$subsystem) {
    $exe = "$work\t.exe"
    if (Test-Path $exe) { Remove-Item $exe -Force }
    $a = @('/nologo', "/out:$exe") + ($set | ForEach-Object { "$work\$_.obj" }) + $libraries
    if ($subsystem -ne '-') { $a += "/subsystem:$subsystem" }
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $o = (& $linker @a 2>&1 | ForEach-Object { "$_" }) -join "`n"
    $rc = $LASTEXITCODE
    $ErrorActionPreference = $saved
    if ($rc -ne 0) {
        if ($o -match 'unresolved external symbols?(?: \(\d+\))?:?\s+(\w+)') { return "undefined $($Matches[1])" }
        return "failed: $o"
    }
    $b = [IO.File]::ReadAllBytes($exe)
    $subsystemOf = @{ 2 = 'windows'; 3 = 'console' }[[int][BitConverter]::ToUInt16($b, [BitConverter]::ToInt32($b, 60) + 24 + 68)]
    $p = Start-Process -FilePath $exe -Wait -PassThru -NoNewWindow
    return "$subsystemOf $($names[$p.ExitCode - 1])"
}

$rows = @()
foreach ($set in $sets) {
    foreach ($subsystem in '-', 'console', 'windows') { $rows += "$($set -join ',') $subsystem $(Linked $set $subsystem)" }
}
if (-not $Record) { $env:LIB = $saved }
$expected = Join-Path $here 'linked.expected'
if ($Record) {
    [IO.File]::WriteAllText($expected, ($rows -join "`n") + "`n")
    "linked.expected recorded from $linker"
    return
}
$want = @([IO.File]::ReadAllLines($expected))
$faults = 0
for ($i = 0; $i -lt $rows.Count; $i++) {
    if ($rows[$i] -ne $want[$i]) { $faults++; "linked: expected '$($want[$i])'"; "linked: got      '$($rows[$i])'" }
}
"linked: $($rows.Count - $faults) of $($rows.Count) as Microsoft's link.exe"
if ($faults) { throw "linked: $faults differ" }
