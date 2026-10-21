# validate-msi.ps1 - what can be checked of a .msi without installing it: its summary information
# (the SDK's MsiInfo), a dump of every table, and the Internal Consistency Evaluators of the SDK's
# darice.cub, run the way msival2 runs them - the .cub merged into a copy of the package, each ICE of
# its _ICESequence run as a custom action through the Installer automation API - with the messages
# taken from the Windows Installer log. Errors fail the script; every warning is reported.
#
#   validate-msi.ps1 -Msi <GRIDEd-ver.msi> [-Cub <darice.cub>] [-OutDir <dist>]
#
# darice.cub ships inside the SDK's MsiVal2-x86_en-us.msi; when no -Cub is given it is taken out of
# that package by an administrative install into <OutDir>\tmp (nothing is installed or registered).
param(
    [Parameter(Mandatory = $true)][string]$Msi,
    [string]$Cub = "",
    [string]$OutDir = "",
    [string]$SdkBin = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x86"
)
$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)   # GRIDEx2 (this copy reads no modules.json)
. (Join-Path $PSScriptRoot "Msi-Com.ps1")
if (-not $OutDir) { $OutDir = Join-Path $ProjectRoot "dist" }
$OutDir = [IO.Path]::GetFullPath($OutDir)
$Msi = (Resolve-Path -LiteralPath $Msi).Path
$stem = [IO.Path]::GetFileNameWithoutExtension($Msi)
New-Item -ItemType Directory -Force "$OutDir\logs", "$OutDir\tmp" | Out-Null
$env:TMP = "$OutDir\tmp"; $env:TEMP = "$OutDir\tmp"
function Say([string]$s) { Write-Output $s }
$failed = $false

# ---- 1. Summary information ------------------------------------------------------------------
$infoLog = "$OutDir\logs\$stem-msiinfo.txt"
& "$SdkBin\MsiInfo.exe" $Msi > $infoLog
$info = Get-Content $infoLog
Say "[1/3] MsiInfo ($infoLog)"
foreach ($want in @("Template(MSI CPU,LangIDs) = x64;1033", "Pages(MSI Version Used) = 200", "Words(MSI Source and Elevation Prompt Type) = 10", "Application = Windows Installer")) {
    if ($info -match [regex]::Escape($want)) { Say "  ok    $want" } else { Say "  WRONG $want"; $failed = $true }
}
$rev = ($info | Where-Object { $_ -match 'Revision = (\{[0-9A-F-]{36}\})' } | ForEach-Object { $Matches[1] })
if ($rev) { Say "  ok    package code $rev" } else { Say "  WRONG no package code"; $failed = $true }

# ---- 2. Every table -----------------------------------------------------------------------------
$inst = New-MsiInstaller
$db = Open-MsiDatabase $inst $Msi 0
$dump = "$OutDir\logs\$stem-tables.txt"
$sb = New-Object System.Text.StringBuilder
$tables = @(Read-MsiRows $db "SELECT ``Name`` FROM ``_Tables``" | ForEach-Object { $_[0] } | Sort-Object)
$counts = @()
foreach ($t in $tables) {
    $cols = @(Read-MsiRows $db "SELECT ``Name`` FROM ``_Columns`` WHERE ``Table`` = '$t'" | ForEach-Object { $_[0] })
    $rows = @(Read-MsiRows $db "SELECT * FROM ``$t``")
    $counts += "$t ($($rows.Count))"
    [void]$sb.Append("== $t ($($rows.Count) rows): " + ($cols -join " | ") + "`r`n")
    foreach ($r in $rows) { [void]$sb.Append("   " + ($r -join " | ") + "`r`n") }
}
$streams = @(Read-MsiRows $db "SELECT ``Name`` FROM ``_Streams``" | ForEach-Object { $_[0] })
[void]$sb.Append("== _Streams: " + ($streams -join ", ") + "`r`n")
[IO.File]::WriteAllText($dump, $sb.ToString(), (New-Object System.Text.UTF8Encoding $false))
$db = $null
Say "[2/3] Tables ($dump)"
Say ("  " + ($counts -join ", "))
Say ("  streams: " + ($streams -join ", "))

# ---- 3. ICE validation --------------------------------------------------------------------------
if (-not $Cub) {
    $Cub = "$OutDir\tmp\msival2\MsiVal2\darice.cub"
    if (-not (Test-Path -LiteralPath $Cub)) {
        $pkg = Join-Path $SdkBin "MsiVal2-x86_en-us.msi"
        if (-not (Test-Path -LiteralPath $pkg)) { throw "no darice.cub and no $pkg to take it from" }
        Say "  extracting darice.cub from $pkg"
        $p = Start-Process msiexec.exe -ArgumentList @('/a', "`"$pkg`"", "TARGETDIR=`"$OutDir\tmp\msival2`"", '/qn') -Wait -PassThru
        if ($p.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $Cub)) { throw "could not extract darice.cub (msiexec /a exit $($p.ExitCode))" }
    }
}
$work = "$OutDir\tmp\ice-$stem"
if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force }
New-Item -ItemType Directory -Force $work | Out-Null
Copy-Item -LiteralPath $Msi -Destination "$work\package.msi"
$iceLog = "$work\ice-run.log"
$inst = New-MsiInstaller
Set-MsiProp $inst "UILevel" @(2)
Invoke-Msi $inst "EnableLog" @("iwearucmopvx", $iceLog) | Out-Null
$db = Open-MsiDatabase $inst "$work\package.msi" 1
$cubDb = Open-MsiDatabase $inst $Cub 0
Invoke-Msi $db "Merge" @($cubDb, "_MergeErrors") | Out-Null
$ices = @(Read-MsiRows $db "SELECT ``Action`` FROM ``_ICESequence`` ORDER BY ``Sequence``" | ForEach-Object { $_[0] })
# Options 0: with the machine state the engine is not restricted, and custom actions may run.
$session = Invoke-Msi $inst "OpenPackage" @($db, 0)
$ran = 0
foreach ($ice in $ices) { try { Invoke-Msi $session "DoAction" @($ice) | Out-Null; $ran++ } catch { Say "  $ice could not be run: $($_.Exception.Message)"; $failed = $true } }
# The session and the databases released outright, or the package copy stays open past the script.
foreach ($o in @($session, $cubDb, $db)) { if ($o -ne $null) { [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($o) } }
$session = $null; $db = $null; $cubDb = $null
[GC]::Collect(); [GC]::WaitForPendingFinalizers(); [GC]::Collect()

# The messages: "ICEnn<tab>type<tab>text[<tab>help<tab>table<tab>column<tab>keys]"; type 0 failure,
# 1 error, 2 warning, 3 informational.
$messages = @()
foreach ($line in Get-Content -LiteralPath $iceLog) {
    if ($line -match '^(ICE\d+[A-Z]?)\t(\d)\t(.*)$') {
        $parts = $Matches[3] -split "`t"
        $messages += [pscustomobject]@{ Ice = $Matches[1]; Type = [int]$Matches[2]; Text = $parts[0]; Where = (@($parts | Select-Object -Skip 2) -join " ") }
    }
}
$errors = @($messages | Where-Object { $_.Type -eq 1 })
$warnings = @($messages | Where-Object { $_.Type -eq 2 })
$failures = @($messages | Where-Object { $_.Type -eq 0 })
$iceReport = "$OutDir\logs\$stem-ice.txt"
$out = @("ICE validation of $Msi with $Cub", "$ran of $($ices.Count) ICEs run; $($errors.Count) errors, $($warnings.Count) warnings, $($failures.Count) failures, $(@($messages | Where-Object { $_.Type -eq 3 }).Count) informational", "")
foreach ($x in ($failures + $errors + $warnings)) { $out += ("{0,-7} {1,-8} {2}  [{3}]" -f $x.Ice, @("FAILURE", "ERROR", "WARNING")[$x.Type], $x.Text, $x.Where) }
$out += ""
foreach ($x in ($messages | Where-Object { $_.Type -eq 3 })) { $out += ("{0,-7} info     {1}" -f $x.Ice, $x.Text) }
[IO.File]::WriteAllLines($iceReport, $out, (New-Object System.Text.UTF8Encoding $false))
Copy-Item -LiteralPath $iceLog -Destination "$OutDir\logs\$stem-ice-run.log" -Force
# Windows Installer lets go of the copy a moment after the objects are released.
for ($try = 0; $try -lt 20; $try++) {
    try { Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction Stop; break } catch { Start-Sleep -Milliseconds 500 }
}
if (Test-Path -LiteralPath $work) { Say "  note: $work is still held by Windows Installer and was left there" }
Say "[3/3] ICE ($iceReport)"
Say "  $ran of $($ices.Count) ICEs run; $($errors.Count) errors, $($warnings.Count) warnings, $($failures.Count) failures"
foreach ($x in ($failures + $errors + $warnings)) { Say ("  {0,-7} {1,-8} {2}  [{3}]" -f $x.Ice, @("FAILURE", "ERROR", "WARNING")[$x.Type], $x.Text, $x.Where) }
if ($errors.Count -gt 0 -or $failures.Count -gt 0) { $failed = $true }
if ($failed) { throw "validation failed" }
Say "validate-msi: passed"
