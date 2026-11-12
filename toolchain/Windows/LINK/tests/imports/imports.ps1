<#
  imports.ps1 - import libraries of one DLL. An import table belongs to the DLL, not to the library that gave a name,
  so a DLL gets one descriptor and one run of words however many libraries its names came from:
  - second.c, on GRIDE's runtime, calling through a second import library of KERNEL32.dll named before the runtime's
    libraries and after them. Link's verbose output must show that library gave the word, or the row tests nothing;
  - start.c, with no runtime, for every way the libraries below can divide its names, against imports.expected, which
    is what Microsoft's link.exe does with them. A row of that file is the libraries; the image's import table in its
    order, each DLL's names sorted, since link.exe's order inside a run changes with the objects and nothing reads it;
    and the exit code, 42 when every call reached its DLL. -Record writes it again from Microsoft's link.exe.
  The library of two DLLs is made by Microsoft's lib.exe, GRIDE's taking objects alone: without Visual Studio its rows
  are left out.

    .\imports.ps1 [-Bin <toolchain folder>] [-Libs <the runtime's folder>] [-Record]
#>
param([string]$Bin = "", [string]$Libs = "", [switch]$Record)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
if (-not $Bin) { $Bin = (Resolve-Path (Join-Path $here '..\..\..\..\..\GRIDEd\x64\Release')).Path }
if (-not $Libs) { $Libs = Join-Path $Bin 'lib\x86_64-windows'; if (-not (Test-Path (Join-Path $Libs 'grtc.lib'))) { $Libs = Join-Path $Bin 'lib' } }
$work = Join-Path $env:TEMP 'gride-link-imports'
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory $work | Out-Null

function Tool([string]$tool, [string[]]$a) {
    $o = & (Join-Path $Bin "$tool.exe") @a 2>&1
    if ($LASTEXITCODE -ne 0) { throw "$tool failed:`n$($o -join "`n")" }
}
function Compile([string]$name) {
    Tool c90 @('-S', (Join-Path $here "$name.c"), '-o', "$work\$name.s", '-arch', 'x86_64-windows', '-O2', '-masm=masm', '-nologo')
    Tool masm @('-t', 'x64', "$work\$name.s", '-o', "$work\$name.obj")
}

# the folder of Microsoft's x64 tools, or $null
function MicrosoftTools {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $null }
    $vs = & $vswhere -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
    if (-not $vs) { return $null }
    $tools = Get-ChildItem (Join-Path $vs 'VC\Tools\MSVC') -Directory | Where-Object { $_.Name -match '^\d+(\.\d+)+$' } |
             Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    if (-not $tools) { return $null }
    return Join-Path $tools.FullName 'bin\Hostx64\x64'
}

$ms = MicrosoftTools
if ($Record -and -not $ms) { throw 'imports: -Record needs Microsoft''s link.exe' }
$linker = if ($Record) { Join-Path $ms 'link.exe' } else { Join-Path $Bin 'link.exe' }

$saved = $env:LIB
$env:LIB = ''
try {
    if (-not $Record) {
        Compile 'second'
        Tool link @('/LIB', "/DEF:$here\second.def", "/OUT:$work\second.lib")
        $grt = @('grtstart', 'grtcompat', 'grtcxx', 'grtc', 'grtwin', 'kernel32') | ForEach-Object { Join-Path $Libs "$_.lib" }
        foreach ($order in 'first', 'last') {
            $exe = "$work\second-$order.exe"
            $libraries = if ($order -eq 'first') { @("$work\second.lib") + $grt } else { $grt + "$work\second.lib" }
            $o = $null
            $ErrorActionPreference = 'Continue'
            $o = (& $linker @('/nologo', '/verbose', "/out:$exe", "$work\second.obj") $libraries 2>&1 | ForEach-Object { "$_" }) -join "`n"
            $rc = $LASTEXITCODE
            $ErrorActionPreference = 'Stop'
            if ($rc -ne 0) { throw "second, $order`: link failed:`n$o" }
            if (-not $o.Contains("took $work\second.lib(KERNEL32.dll)")) { throw "second, $order`: second.lib gave nothing, so this tests nothing" }
            $ErrorActionPreference = 'Continue'
            $said = (& $exe 2>&1 | ForEach-Object { "$_" }) -join "`n"
            $rc = $LASTEXITCODE
            $ErrorActionPreference = 'Stop'
            if ($rc -ne 0 -or $said -ne 'GetConsoleCP answered') { throw "second, $order`: exits $rc, saying '$said'" }
            "second: second.lib named $order, the program starts and the call answers"
        }
    }

    Compile 'start'
    $defs = [ordered]@{
        kernel = 'KERNEL32.dll', 'ExitProcess', 'GetTickCount'
        more   = 'KERNEL32.dll', 'GetConsoleCP', 'GetCurrentProcessId'
        lower  = 'kernel32.dll', 'GetConsoleCP', 'GetCurrentProcessId'
        upper  = 'KERNEL32.DLL', 'GetConsoleCP', 'GetCurrentProcessId'
        exit   = 'KERNEL32.dll', 'ExitProcess'
        tick   = 'KERNEL32.dll', 'GetTickCount'
        user   = 'USER32.dll', 'AnyPopup', 'GetDesktopWindow'
    }
    foreach ($name in $defs.Keys) {
        $text = @("LIBRARY $($defs[$name][0])", 'EXPORTS') + ($defs[$name][1..9] | ForEach-Object { "    $_" })
        Set-Content "$work\$name.def" $text -Encoding ASCII
        Tool link @('/LIB', "/DEF:$work\$name.def", "/OUT:$work\$name.lib")
    }
    $sets = @('kernel', 'more', 'user'), @('more', 'kernel', 'user'), @('kernel', 'user', 'more'), @('user', 'kernel', 'more'),
            @('more', 'user', 'kernel'), @('exit', 'tick', 'more', 'user'), @('kernel', 'lower', 'user'),
            @('lower', 'kernel', 'user'), @('kernel', 'upper', 'user'), @('upper', 'kernel', 'user')
    if ($ms) {
        $o = & (Join-Path $ms 'lib.exe') @('/nologo', "/OUT:$work\both.lib", "$work\user.lib", "$work\kernel.lib") 2>&1
        if ($LASTEXITCODE -ne 0) { throw "lib failed:`n$($o -join "`n")" }
        $sets += @('both', 'more'), @('more', 'both')
    }

    # an image's import table in its order; a DLL whose two tables disagree says so
    function Imports([string]$exe) {
        $b = [IO.File]::ReadAllBytes($exe)
        $pe = [BitConverter]::ToInt32($b, 60)
        $headers = $pe + 24 + [BitConverter]::ToUInt16($b, $pe + 20)
        $count = [BitConverter]::ToUInt16($b, $pe + 6)
        function Offset([uint32]$rva) {
            for ($i = 0; $i -lt $count; $i++) {
                $s = $headers + 40 * $i
                $from = [BitConverter]::ToUInt32($b, $s + 12)
                if ($rva -ge $from -and $rva -lt $from + [BitConverter]::ToUInt32($b, $s + 16)) {
                    return $rva - $from + [BitConverter]::ToUInt32($b, $s + 20)
                }
            }
            throw "$exe`: RVA $rva is in no section"
        }
        function Text([long]$at) {
            $end = $at
            while ($b[$end] -ne 0) { $end++ }
            return [Text.Encoding]::ASCII.GetString($b, $at, $end - $at)
        }
        $table = @()
        $d = Offset ([BitConverter]::ToUInt32($b, $pe + 24 + 120))
        for (; [BitConverter]::ToUInt32($b, $d + 12) -ne 0; $d += 20) {
            $lookup = Offset ([BitConverter]::ToUInt32($b, $d))
            $address = Offset ([BitConverter]::ToUInt32($b, $d + 16))
            $names = @()
            for (;;) {
                $word = [BitConverter]::ToUInt64($b, $lookup)
                if ($word -ne [BitConverter]::ToUInt64($b, $address)) { $names += '(the two tables differ)' }
                if ($word -eq 0) { break }
                $names += Text ((Offset ([uint32]$word)) + 2)
                $lookup += 8
                $address += 8
            }
            $table += "$(Text (Offset ([BitConverter]::ToUInt32($b, $d + 12)))): $(($names | Sort-Object) -join ' ')"
        }
        return $table -join '; '
    }

    $rows = @()
    foreach ($set in $sets) {
        $exe = "$work\$($set -join '-').exe"
        $a = @('/nologo', '/nodefaultlib', '/entry:start', '/subsystem:console', "/out:$exe", "$work\start.obj") + ($set | ForEach-Object { "$work\$_.lib" })
        $o = & $linker @a 2>&1
        if ($LASTEXITCODE -ne 0) { throw "$($set -join ' '): link failed:`n$($o -join "`n")" }
        $p = Start-Process -FilePath $exe -Wait -PassThru -NoNewWindow
        $rows += "$($set -join ' ') | $(Imports $exe) | $($p.ExitCode)"
    }
} finally { $env:LIB = $saved }

$expected = Join-Path $here 'imports.expected'
if ($Record) {
    [IO.File]::WriteAllText($expected, ($rows -join "`n") + "`n")
    "imports.expected recorded from $linker"
    return
}
$want = @([IO.File]::ReadAllLines($expected) | Where-Object { $ms -or $_ -notmatch '\bboth\b' })
$faults = 0
for ($i = 0; $i -lt $rows.Count; $i++) {
    if ($rows[$i] -ne $want[$i]) { $faults++; "imports: expected '$($want[$i])'"; "imports: got      '$($rows[$i])'" }
}
"imports: $($rows.Count - $faults) of $($rows.Count) as Microsoft's link.exe"
if ($faults) { throw "imports: $faults differ" }
