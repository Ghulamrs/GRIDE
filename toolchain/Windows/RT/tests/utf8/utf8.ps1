<#
  utf8.ps1 - UTF-8 console and paths (docs\Runtime_Design.md 6), asked for by setlocale(LC_CTYPE, ".UTF-8"):
  - utf8.cpp through probe.ps1: every call that takes a path, the conversions, printf's %ls, against the UCRT's
    transcript; before asking, after asking, and back in "C";
  - args.cpp: argv, the environment and the names Windows hands back, in a folder and on a file whose names are past
    the ANSI code page. Asked, in main or in a static object's constructor, GRIDE's runtime must give each in exactly
    these UTF-8 bytes (and the name on the command line must open); not asked, byte for byte what the UCRT gives;
  - console.cpp under screen.c, on a hidden console of its own: asked, what is on the screen must be console.expected,
    which needs no code page, and the console's code pages must be as they were; not asked, the same screen as the
    UCRT's. console.expected was written from console.cpp's text and the keys below, not from a run, so there is no
    -Record for it. Its U+FFFD is the character past one UTF-16 unit: ReadConsoleOutputCharacterW answers that for
    it whoever wrote it, the console's own echo of the keys too; getchar's bytes show it arrived whole. The characters
    here are written by number, because Windows PowerShell reads a script in the ANSI code page.

    .\utf8.ps1 [-Bin <toolchain folder>] [-Libs <the runtime's folder>] [-Opt -O2|-O0] [-DebugRuntime]
#>
param([string]$Bin = "", [string]$Libs = "", [string]$Opt = '-O2', [switch]$DebugRuntime)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
if (-not $Bin) { $Bin = (Resolve-Path (Join-Path $here '..\..\..\..\..\GRIDEd\x64\Release')).Path }
if (-not $Libs) { $Libs = Join-Path $Bin 'lib\x86_64-windows'; if (-not (Test-Path (Join-Path $Libs 'grtc.lib'))) { $Libs = Join-Path $Bin 'lib' } }
$work = Join-Path $env:TEMP 'gride-utf8'
$utf8 = New-Object Text.UTF8Encoding $false

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

$probeArguments = @{ Source = (Join-Path $here 'utf8.cpp'); Opt = $Opt; Bin = $Bin; Libs = $Libs }
if ($DebugRuntime) { $probeArguments.DebugRuntime = $true }
& (Join-Path $here '..\probe.ps1') @probeArguments

if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory $work | Out-Null

function Compile([string]$source) {
    $name = [IO.Path]::GetFileNameWithoutExtension($source)
    $compiler = if ([IO.Path]::GetExtension($source) -eq '.c') { 'c90' } else { 'cpp11' }
    Tool $compiler @('-S', (Join-Path $here $source), '-o', "$work\$name.s", '-arch', 'x86_64-windows', $Opt, '-masm=masm', '-nologo')
    Tool masm @('-t', 'x64', "$work\$name.s", '-o', "$work\$name.obj")
}
foreach ($source in 'args.cpp', 'console.cpp', 'screen.c') { Compile $source }
Tool link @('/LIB', "/DEF:$here\screen.def", "/OUT:$work\screen.lib")

$d = if ($DebugRuntime) { 'd' } else { '' }
$grt = @(@('grtstart', 'grtcompat', 'grtcxx', 'grtc', 'grtwin') | ForEach-Object { Join-Path $Libs "$_$d.lib" }) + (Join-Path $Libs 'kernel32.lib')
$saved = $env:LIB
try {
    $env:LIB = ''
    Tool link (@('/nologo', '/subsystem:console', "/out:$work\args-grt.exe", "$work\args.obj") + $grt)
    Tool link (@('/nologo', '/subsystem:console', "/out:$work\console-grt.exe", "$work\console.obj") + $grt)
    Tool link (@('/nologo', '/subsystem:console', "/out:$work\screen.exe", "$work\screen.obj") + $grt + "$work\screen.lib")
    $ms = MicrosoftLib
    if ($ms) {
        $env:LIB = $ms
        foreach ($name in 'args', 'console') {
            Tool link @('/nologo', '/subsystem:console', "/out:$work\$name-msvc.exe", "$work\$name.obj", "libcmt$d.lib", "libucrt$d.lib",
                        "libvcruntime$d.lib", 'kernel32.lib', 'legacy_stdio_definitions.lib')
        }
    }
} finally { $env:LIB = $saved }

$faults = 0
function Fault([string]$what) { $script:faults++; "  FAULT: $what" }

# the characters: Greek, Japanese and one past a UTF-16 unit, none of them in code page 1252 or 437
$smile = [char]::ConvertFromUtf32(0x1F600)
$greek = -join [char[]](0x3B1, 0x3B2, 0x3B3)
$japanese = -join [char[]](0x65E5, 0x672C, 0x8A9E)
$folderName = "folder-$japanese"
$fileName = "$greek-$smile.txt"
$value = "value-$greek-$japanese"
$folder = Join-Path $work $folderName
New-Item -ItemType Directory $folder | Out-Null
[IO.File]::WriteAllText((Join-Path $folder $fileName), "held`n", $utf8)

# args run in the folder with stdout a pipe: its bytes and exit code
function Run-Args([string]$exe, [string]$mode, [hashtable]$environment) {
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $exe
    $info.Arguments = "$mode `"$fileName`""
    $info.WorkingDirectory = $folder
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.EnvironmentVariables['GRT_PROBE'] = $value
    foreach ($n in $environment.Keys) { $info.EnvironmentVariables[$n] = $environment[$n] }
    $p = [Diagnostics.Process]::Start($info)
    $bytes = New-Object IO.MemoryStream
    $p.StandardOutput.BaseStream.CopyTo($bytes)
    $p.WaitForExit()
    if ($p.ExitCode -ne 0) { Fault "$exe $mode exits $($p.ExitCode)" }
    return , $bytes.ToArray()
}

function Lines-Expected([string]$mode, [int]$early) {
    return @(
        "argc 3 early $early"
        "argv $fileName"
        "the string taken first @ bytes, still @"
        'fopen 1 errno 0'
        'holds held'
        "getenv $value"
        'getenv taken first @ bytes'
        "envp GRT_PROBE=$value"
        "getcwd $folderName"
        "found $fileName"
    )
}

foreach ($case in @(@{ Mode = 'utf8'; Early = 0; Environment = @{} }, @{ Mode = 'early'; Early = 1; Environment = @{ GRT_PROBE_EARLY = '1' } })) {
    $bytes = Run-Args "$work\args-grt.exe" $case.Mode $case.Environment
    $got = $utf8.GetString($bytes).Replace("`r`n", "`n").TrimEnd("`n") -split "`n"
    $want = Lines-Expected $case.Mode $case.Early
    $wrong = 0
    for ($i = 0; $i -lt [Math]::Max($got.Count, $want.Count); $i++) {
        $w = "$($want[$i])"
        $g = "$($got[$i])"
        if ($w -match '@') { $g = $g -replace '\d+', '@' }
        if ($w -cne $g) {
            $wrong++
            Fault "args $($case.Mode), line $($i + 1): expected '$w', got '$g'"
        }
    }
    "args $($case.Mode) grt: $($want.Count) lines, $wrong wrong"
}
if ($ms) {
    $a = Run-Args "$work\args-grt.exe" 'plain' @{}
    $b = Run-Args "$work\args-msvc.exe" 'plain' @{}
    if ([BitConverter]::ToString($a) -ne [BitConverter]::ToString($b)) { Fault 'args plain: grt and the UCRT differ' }
    else { "args plain: $($a.Length) bytes, the same as the UCRT's" }
} else { 'args plain: not compared (no Visual Studio here)' }

# a program on a console of screen.exe's: its transcript, in UTF-8
function Run-Screen([string]$program, [string]$mode, [string]$keys) {
    $name = [IO.Path]::GetFileNameWithoutExtension($program)
    $keyFile = "$work\$name-$mode.keys"
    [IO.File]::WriteAllText($keyFile, $keys, (New-Object Text.UnicodeEncoding $false, $false))
    $p = Start-Process -FilePath "$work\screen.exe" -ArgumentList "`"$keyFile`" `"$program`" $mode" -WorkingDirectory $work -WindowStyle Hidden -Wait -PassThru `
                       -RedirectStandardOutput "$work\$name-$mode.screen" -RedirectStandardError "$work\$name-$mode.err"
    if ($p.ExitCode -ne 0) { Fault "screen $name $mode exits $($p.ExitCode)" }
    return [IO.File]::ReadAllText("$work\$name-$mode.screen", $utf8).Replace("`r`n", "`n")
}

$typedName = "typed-$greek-$japanese.txt"
$ctrlZ = [string][char]0x1A
$keys = "$typedName`n$greek$japanese$smile`n$ctrlZ`n"
$screen = Run-Screen "$work\console-grt.exe" 'utf8' $keys
$expected = [IO.File]::ReadAllText((Join-Path $here 'console.expected'), $utf8).Replace("`r`n", "`n")
# the long line, whatever width the console has
$zhe = [string][char]0x436
$screen = [regex]::Replace($screen, "(?<=[x$zhe])\n(?=$zhe)", '')
if ($screen -ceq $expected) { 'console utf8: the screen is console.expected, and the code pages as they were' }
else {
    $e = $expected -split "`n"
    $g = $screen -split "`n"
    for ($i = 0; $i -lt [Math]::Max($e.Count, $g.Count); $i++) {
        if ("$($e[$i])" -cne "$($g[$i])") { Fault "console utf8, line $($i + 1): expected '$($e[$i])', got '$($g[$i])'" }
    }
    "console utf8: output in $work\console-grt-utf8.screen"
}
if (Test-Path -LiteralPath (Join-Path $work $typedName)) { 'console utf8: the file named at the keyboard is there' }
else { Fault 'console utf8: no file by the typed name' }

if ($ms) {
    $plain = "abc`ndef`n$ctrlZ`n"
    $a = Run-Screen "$work\console-grt.exe" 'plain' $plain
    $b = Run-Screen "$work\console-msvc.exe" 'plain' $plain
    if ($a -cne $b) { Fault "console plain: grt and the UCRT differ ($work\console-grt-plain.screen, console-msvc-plain.screen)" }
    else { "console plain: $(($a -split "`n").Count) lines, the same screen as the UCRT's" }
} else { 'console plain: not compared (no Visual Studio here)' }

if ($faults) { throw "utf8: $faults fault(s)" }
'utf8: all as expected'
