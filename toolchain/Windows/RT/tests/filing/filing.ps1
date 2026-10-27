<#
  filing.ps1 - the filing component's UCRT probe (docs\Runtime_Design.md 11.2): probe.cpp compiled once by cpp11 and
  echo.c by c90, linked with GRIDE's runtime alone (LIB empty, the six libraries by path) and, where Visual Studio's
  and the SDK's libraries are found, with Microsoft's static CRT as GRIDE links it. Each is run on a tree of files
  made afresh for it - fixed times, read-only and normal attributes, a hard link, a junction, a file past 2 GB, a
  command processor of the probe's own (echo.c) for system - and its transcript, the tree's path written <ROOT>,
  compared with filing.expected, which the UCRT's run recorded. -Record writes filing.expected from the UCRT's run.

    .\filing.ps1 [-Bin <toolchain folder>] [-Libs <the runtime's folder>] [-Record]
#>
param([string]$Bin = "", [string]$Libs = "", [switch]$Record)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
if (-not $Bin) { $Bin = (Resolve-Path (Join-Path $here '..\..\..\..\..\GRIDEd\x64\Release')).Path }
if (-not $Libs) { $Libs = Join-Path $Bin 'lib' }
$work = Join-Path $env:TEMP 'gride-filing'
$root = Join-Path $work 'root'

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

function Clear-Tree {
    if (Test-Path "$root\junc") { [IO.Directory]::Delete("$root\junc") }
    if (Test-Path $root) {
        Get-ChildItem $root -Recurse -File | ForEach-Object { $_.Attributes = 'Normal' }
        Remove-Item $root -Recurse -Force
    }
}

function New-Tree {
    Clear-Tree
    New-Item -ItemType Directory $root, "$root\sub", "$root\bin1", "$root\bin2" | Out-Null
    $k = 0
    foreach ($f in 'file.txt', 'normal.txt', 'readonly.txt', 'prog.EXE', 'run.Cmd', 'x.cmd.txt', 'sub\inner.txt', 'link1.txt', 'old.txt') {
        [IO.File]::WriteAllText("$root\$f", ('x' * (5 + $k)))
        $k++
    }
    New-Item -ItemType HardLink -Path "$root\link2.txt" -Target "$root\link1.txt" | Out-Null
    Copy-Item "$work\echo.exe" "$root\bin2\cmd.exe"
    Copy-Item "$work\echo.exe" "$root\bin1\shell.exe"
    $big = [IO.File]::Create("$root\big.bin")
    $big.SetLength(2147483649)
    $big.Close()
    $t = New-Object DateTime 2001, 2, 3, 4, 5, 6, ([DateTimeKind]::Utc)
    foreach ($f in Get-ChildItem $root -Recurse -File) {
        $f.CreationTimeUtc = $t.AddDays(-1)
        $f.LastWriteTimeUtc = $t
        $f.LastAccessTimeUtc = $t.AddHours(1)
    }
    $old = Get-Item "$root\old.txt"
    $old.LastWriteTimeUtc = New-Object DateTime 1960, 6, 1, 12, 0, 0, ([DateTimeKind]::Utc)
    $old.CreationTimeUtc = New-Object DateTime 1960, 5, 1, 12, 0, 0, ([DateTimeKind]::Utc)
    (Get-Item "$root\normal.txt").Attributes = 'Normal'
    (Get-Item "$root\readonly.txt").Attributes = 'ReadOnly, Archive'
    New-Item -ItemType Junction -Path "$root\junc" -Target "$root\sub" | Out-Null
}

# the probe run once in folder with the environment changes given ($null removes a name): its lines and exit code
function Run([string]$exe, [string]$folder, [string[]]$arguments, [hashtable]$environment) {
    $saved = @{}
    foreach ($n in $environment.Keys) {
        $saved[$n] = [Environment]::GetEnvironmentVariable($n)
        [Environment]::SetEnvironmentVariable($n, $environment[$n])
    }
    try {
        $p = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $folder -NoNewWindow -Wait -PassThru `
                           -RedirectStandardOutput "$work\run.out" -RedirectStandardError "$work\run.err"
    } finally {
        foreach ($n in $saved.Keys) { [Environment]::SetEnvironmentVariable($n, $saved[$n]) }
    }
    $lines = @(Get-Content "$work\run.out") + @(Get-Content "$work\run.err" | ForEach-Object { "stderr: $_" })
    return $lines + "exit $($p.ExitCode)"
}

function Transcript([string]$exe) {
    New-Tree
    $drive = $root.Substring(0, 1).ToUpperInvariant()
    $path = "$root\bin1;;`"$root\bin2`";$env:SystemRoot\System32"
    $t = @('== stat') + (Run $exe $root @('stat', $drive) @{})
    $t += @('== find') + (Run $exe $root @('find') @{})
    $t += @('== cwd') + (Run $exe $root @('cwd', "`"$root`"") @{})
    $t += @('== system: COMSPEC names a shell') + (Run $exe "$root\sub" @('system') @{ COMSPEC = "$root\bin1\shell.exe" })
    $t += @('== system: COMSPEC names nothing, cmd.exe in the working folder') + (Run $exe "$root\bin2" @('system') @{ COMSPEC = "$root\nothere\shell.exe" })
    $t += @('== system: no COMSPEC, cmd.exe on PATH') + (Run $exe "$root\sub" @('system') @{ COMSPEC = $null; PATH = $path })
    Clear-Tree
    return (($t | ForEach-Object { $_.Replace($root, '<ROOT>') }) -join "`n") + "`n"
}

if (Test-Path $work) { Clear-Tree; Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory $work | Out-Null
try {
    Tool cpp11 @('-S', "$here\probe.cpp", '-o', "$work\probe.s", '-arch', 'x86_64-windows', '-O2', '-masm=masm', '-nologo')
    Tool masm @('-t', 'x64', "$work\probe.s", '-o', "$work\probe.obj")
    Tool c90 @('-S', "$here\echo.c", '-o', "$work\echo.s", '-arch', 'x86_64-windows', '-O2', '-masm=masm', '-nologo')
    Tool masm @('-t', 'x64', "$work\echo.s", '-o', "$work\echo.obj")
    $grt = @('grtstart', 'grtcompat', 'grtcxx', 'grtc', 'grtwin', 'kernel32') | ForEach-Object { Join-Path $Libs "$_.lib" }
    $saved = $env:LIB
    try {
        $env:LIB = ''
        Tool link (@('/nologo', '/subsystem:console', "/out:$work\probe-grt.exe", "$work\probe.obj") + $grt)
        Tool link (@('/nologo', '/subsystem:console', "/out:$work\echo.exe", "$work\echo.obj") + $grt)
        $ms = MicrosoftLib
        if ($ms) {
            $env:LIB = $ms
            Tool link @('/nologo', '/subsystem:console', "/out:$work\probe-msvc.exe", "$work\probe.obj", 'libcmt.lib', 'libucrt.lib',
                        'libvcruntime.lib', 'kernel32.lib', 'legacy_stdio_definitions.lib')
        }
    } finally { $env:LIB = $saved }

    $expectedFile = Join-Path $here 'filing.expected'
    $results = @{}
    $results['grt'] = Transcript "$work\probe-grt.exe"
    [IO.File]::WriteAllText("$work\grt.txt", $results['grt'])
    if ($ms) {
        $results['msvc'] = Transcript "$work\probe-msvc.exe"
        [IO.File]::WriteAllText("$work\msvc.txt", $results['msvc'])
        if ($Record) { [IO.File]::WriteAllText($expectedFile, $results['msvc']); "filing.expected recorded from the UCRT's run" }
    } elseif ($Record) { throw 'filing: -Record needs Microsoft''s CRT, and no Visual Studio was found' }
    $expected = [IO.File]::ReadAllText($expectedFile).Replace("`r`n", "`n")
    $faults = 0
    foreach ($k in $results.Keys | Sort-Object) {
        if ($results[$k] -eq $expected) { "${k}: the same as filing.expected ($(($expected -split "`n").Count - 1) lines)" }
        else {
            $faults++
            "${k}: differs from filing.expected (transcript: $work\$k.txt):"
            $e = $expected -split "`n"
            $g = $results[$k] -split "`n"
            for ($i = 0; $i -lt [Math]::Max($e.Count, $g.Count); $i++) {
                if ($e[$i] -ne $g[$i]) { "  line $($i + 1): expected '$($e[$i])'"; "  line $($i + 1): got      '$($g[$i])'" }
            }
        }
    }
    if (-not $ms) { 'msvc: not run (no Visual Studio here); grt compared with the recorded UCRT transcript alone' }
    if ($faults) { throw "filing: $faults transcript(s) differ" }
} finally {
    Clear-Tree
}
