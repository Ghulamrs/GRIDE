<#
  access.ps1 - Shalimar's direct access to the runtime (docs\Runtime_Design.md 14): each program under all\ built and
  run four ways - x86_64-windows over GRIDE's runtime and over Microsoft's, tms6747 under sim6747 and under vm6747 -
  and hosts\ the first two, against <name>.expected: what the program writes, then its exit code. <name>.<way>.expected
  takes over for one way (grt, msvc, sim6747, vm6747), <name>.tms6747.expected for the two under emulation. refused\ must not compile: each file's first line says what the compiler answers, for
  every target or, as `refused tms6747:`, for that one.

    .\access.ps1 [-Bin <toolchain folder>] [-Out <folder>] [-DebugRuntime] [-Refresh]

  -Refresh writes the expected files from the first way that runs, for reading before they are committed.
#>
param([string]$Bin = "", [string]$Out = "", [switch]$DebugRuntime, [switch]$Refresh)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = (Resolve-Path (Join-Path $here '..\..\..\..\..')).Path
if (-not $Bin) { $Bin = Join-Path $root 'GRIDEd\x64\Release' }
if (-not $Out) { $Out = Join-Path $env:TEMP 'gride-shalimar-access' }
$Out = (New-Item -ItemType Directory -Force $Out).FullName
$win = Join-Path $Bin 'lib\x86_64-windows'
$c6x = Join-Path $Bin 'lib\tms6747'
$d = if ($DebugRuntime) { 'd' } else { '' }
# -g on x86_64-windows would write clang's spelling, which masm does not read
$windowsFlag = if ($DebugRuntime) { @('--debug') } else { @() }
$debugFlag = if ($DebugRuntime) { @('-g') } else { @() }

$linkCommands = Join-Path $Out 'ti-link.cmd'
@('--rom_model', '--stack_size=0x4000', '--heap_size=0x100000', 'MEMORY', '{', '    RAM : origin = 0xC0000000, length = 0x04000000', '}',
  'SECTIONS', '{', '    .text        > RAM', '    .const       > RAM', '    .data        > RAM', '    .bss         > RAM',
  '    .far         > RAM', '    .fardata     > RAM', '    .neardata    > RAM', '    .rodata      > RAM', '    .cinit       > RAM',
  '    .init_array  > RAM', '    .switch      > RAM', '    .cio         > RAM', '    .stack       > RAM', '    .sysmem      > RAM',
  '    .vm6747.eh   > RAM', '}') | Set-Content -Encoding ASCII $linkCommands

function Run([string]$exe, [string[]]$arguments, [string]$lib = $null) {
    $line = ($arguments | ForEach-Object { if ($_ -match '[\s"]') { '"' + $_ + '"' } else { $_ } }) -join ' '
    $start = New-Object Diagnostics.ProcessStartInfo $exe, $line
    $start.UseShellExecute = $false; $start.CreateNoWindow = $true; $start.WorkingDirectory = $Out
    $start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true; $start.RedirectStandardInput = $true
    if ($null -ne $lib) { $start.EnvironmentVariables['LIB'] = $lib }
    $p = [Diagnostics.Process]::Start($start)
    $p.StandardInput.Close()
    $o = $p.StandardOutput.ReadToEndAsync(); $e = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit(120000)) { $p.Kill(); return @{ Code = -1; Text = "no answer in two minutes`n" } }
    $p.WaitForExit()
    return @{ Code = $p.ExitCode; Text = $o.Result + $e.Result }
}
function Tool([string]$name) { Join-Path $Bin "$name.exe" }
function Must($result, [string]$what) { if ($result.Code -ne 0) { throw "${what}: $($result.Text)" } }

# Visual Studio's libraries for the way over Microsoft's runtime, found as the toolchain demo finds them
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = (& $vswhere -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
$msvc = Get-ChildItem (Join-Path $vs 'VC\Tools\MSVC') -Directory | Sort-Object { [version]$_.Name } -Descending
$msvc = @($msvc | Where-Object Name -like '14.44.*') + @($msvc) | Select-Object -First 1
$kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Lib'
$sdk = Get-ChildItem $kits -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'um\x64') } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
$msvcLib = (Join-Path $msvc.FullName 'lib\x64') + ';' + (Join-Path $sdk.FullName 'ucrt\x64') + ';' + (Join-Path $sdk.FullName 'um\x64')
$failures = 0; $passed = 0

function Transcript($run) {
    $lines = ($run.Text -replace "`r`n", "`n") -split "`n" | Where-Object { $_ -notmatch '^CYCLES ' }
    return (($lines -join "`n").TrimEnd() + "`n[exit $($run.Code)]`n")
}
function Against([string]$src, [string]$way, $run) {
    $name = [IO.Path]::GetFileNameWithoutExtension($src)
    $folder = Split-Path -Parent $src
    $file = Join-Path $folder "$name.$way.expected"
    if (-not (Test-Path $file) -and $way -match '6747$') { $file = Join-Path $folder "$name.tms6747.expected" }
    if (-not (Test-Path $file)) { $file = Join-Path $folder "$name.expected" }
    $got = Transcript $run
    if ($Refresh -and -not (Test-Path $file)) { [IO.File]::WriteAllText($file, $got, (New-Object Text.UTF8Encoding($false))) }
    $wanted = if (Test-Path $file) { [IO.File]::ReadAllText($file) -replace "`r`n", "`n" } else { '' }
    if ($got -ceq $wanted) { $script:passed++; return }
    $script:failures++
    "FAILED $name on ${way}:"
    "  wanted: $($wanted.TrimEnd() -replace "`n", ' | ')"
    "  got:    $($got.TrimEnd() -replace "`n", ' | ')"
}
function Windows([string]$src) {
    $name = [IO.Path]::GetFileNameWithoutExtension($src)
    $asm = Join-Path $Out "$name-win.s"
    Must (Run (Tool shalimar) (@('-S', $src, '-o', $asm, '--target=x86_64-windows') + $windowsFlag)) "shalimar $name"
    $obj = Join-Path $Out "$name-win.obj"
    Must (Run (Tool masm) @('-t', 'x64', $asm, '-o', $obj)) "masm $name"
    $exe = Join-Path $Out "$name-grt.exe"
    $libs = @("$win\shmrt$d.lib") + (@('grtstart', 'grtcompat', 'grtcxx', 'grtc', 'grtwin') | ForEach-Object { "$win\$_$d.lib" }) + "$win\kernel32.lib"
    Must (Run (Tool link) (@('/nologo', '/subsystem:console', "/out:$exe", $obj) + $libs) '') "link $name over grt"
    Against $src 'grt' (Run $exe @())
    $exe = Join-Path $Out "$name-msvc.exe"
    $crt = if ($DebugRuntime) { @('libcmtd.lib', 'libucrtd.lib', 'libvcruntimed.lib') } else { @('libcmt.lib', 'libucrt.lib', 'libvcruntime.lib') }
    $libs = @("$win\msvc\shmrt$d.lib") + $crt + @('kernel32.lib', 'legacy_stdio_definitions.lib')
    Must (Run (Tool link) (@('/nologo', '/subsystem:console', "/out:$exe", $obj) + $libs) $msvcLib) "link $name over Microsoft's"
    Against $src 'msvc' (Run $exe @())
}
function C6747([string]$src) {
    $name = [IO.Path]::GetFileNameWithoutExtension($src)
    $asm = Join-Path $Out "$name-6x.s"
    Must (Run (Tool shalimar) (@('-S', $src, '-o', $asm, '--target=tms6747') + $debugFlag)) "shalimar $name for the C6747"
    Against $src 'vm6747' (Run (Tool vm6747) @($asm, (Join-Path $c6x "shmrt$(if ($DebugRuntime) { '-debug' })")))
    $obj = Join-Path $Out "$name-6x.obj"
    Must (Run (Tool asm6x) @($asm, '-o', $obj)) "asm6x $name"
    $image = Join-Path $Out "$name.out"
    Must (Run (Tool lnk6x) @('-mv6740', '--abi=eabi', '-i', $c6x, $linkCommands, $obj, '-l', "shmrt6x$d.lib", '-l', "rts6x$d.lib", '-o', $image)) "lnk6x $name"
    Against $src 'sim6747' (Run (Tool sim6747) @('--run', '-c', $image))
}
function Refused([string]$src) {
    $first = (Get-Content $src -TotalCount 1)
    if ($first -notmatch '^// refused( tms6747)?: (.*)$') { throw "$src says nothing of what it expects" }
    $only = $Matches[1]; $wanted = $Matches[2]
    $targets = if ($only) { @('tms6747') } else { @('x86_64-windows', 'tms6747') }
    foreach ($target in $targets) {
        $r = Run (Tool shalimar) @('-S', $src, '-o', (Join-Path $Out 'refused.s'), "--target=$target")
        if ($r.Code -ne 0 -and $r.Text.Contains($wanted)) { $script:passed++; continue }
        $script:failures++
        "FAILED $([IO.Path]::GetFileName($src)) on ${target}: exit $($r.Code), wanted [$wanted]: $(($r.Text -split "`n" | Where-Object { $_ -match 'error' }) -join ' | ')"
    }
}

foreach ($f in Get-ChildItem (Join-Path $here 'all') -Filter *.shl | Sort-Object Name) { Windows $f.FullName; C6747 $f.FullName }
foreach ($f in Get-ChildItem (Join-Path $here 'hosts') -Filter *.shl | Sort-Object Name) { Windows $f.FullName }
foreach ($f in Get-ChildItem (Join-Path $here 'refused') -Filter *.shl | Sort-Object Name) { Refused $f.FullName }

"$passed passed, $failures failed"
if ($failures) { exit 1 }
