# build-setup.ps1 - GRIDE-Setup-<ver>.msi: one Windows Installer package that installs one outlook of
# GRIDEx2 - Black (GRIDEd), Grey (GRIDE 2.0) or White (RIDE 5.17 from GRIDE 1.0, RIDE - The Simplest) - each
# with the toolchain beside it, chosen on the second screen: one of the three, White a choice like the
# others (the user's design, 2026-10-18). What the user sees names GRIDE and its outlooks:
# the screens, the folders GRIDE\Black, GRIDE\Grey, GRIDE\White, the Start menu, Programs and Features. Written through Windows Installer's own automation API (msi.dll): no WiX, no Inno
# Setup, no setup project. Adapted from GRIDE-Installer's build-msi.ps1 (one IDE, dummy tools).
#
#   build-setup.ps1 [-Version 1.0.0] [-OutDir <GRIDEx2\Installer\dist>]
#
# Input: each solution's Release build, as Visual Studio's Build Solution leaves it -
# GRIDEd\x64\Release and GRIDE 2.0\x64\Release (GRIDE.exe, the ten programs, toolchain.json, docs\,
# lib\) - and each IDE's samples\cppparse, samples\shalimar and samples\showcase (installed under examples\). Build both first (one at a time). Left out: .pdb, .ilk,
# .metagen. Added: Visual Studio 2022's VC++ runtime DLLs beside GRIDE.exe (app-local, as Microsoft
# allows for Microsoft.VC143.CRT), so a machine without the VC++ Redistributable runs it.
#
# The package: features Black (level 1), Grey and White (level 3, so not installed unless chosen); the
# second dialog's Install button removes the outlooks not chosen and adds the chosen one. A quiet
# install takes IDE=Grey or IDE=White (Condition table). ALLUSERS=2 with the UAC-compliant bit: MSIINSTALLPERUSER=1 installs
# under the user's profile without elevation. Launch conditions: 64-bit Windows, .NET Framework 4.8,
# no newer GRIDEx2. Component GUIDs are a hash of each file's install path, the ProductCode a hash of
# the version, the UpgradeCode fixed.
param(
    [string]$Version = "1.0.0",
    [string]$OutDir = "",
    [string]$VsRoot = "D:\Microsoft Visual Studio 2026",
    [string]$CrtFolder = "",
    [switch]$NoUi
)
$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2
. (Join-Path $PSScriptRoot "msi-schema.ps1")
. (Join-Path $PSScriptRoot "setup-ui.ps1")
. (Join-Path $PSScriptRoot "Msi-Com.ps1")
if ($Version -notmatch '^\d{1,3}\.\d{1,3}\.\d{1,5}$') { throw "the version must be major.minor.build: $Version" }
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)          # GRIDEx2
if (-not $OutDir) { $OutDir = Join-Path $root "Installer\dist" }
$OutDir = [IO.Path]::GetFullPath($OutDir)
New-Item -ItemType Directory -Force "$OutDir\tmp", "$OutDir\logs" | Out-Null
$env:TMP = "$OutDir\tmp"; $env:TEMP = "$OutDir\tmp"
function Say([string]$s) { Write-Output $s }

$script:md5 = [Security.Cryptography.MD5]::Create()
function Get-HashHex([string]$s) { ($script:md5.ComputeHash([Text.Encoding]::UTF8.GetBytes($s)) | ForEach-Object { $_.ToString("X2") }) -join "" }
function Get-HashGuid([string]$s) { $h = Get-HashHex $s; "{" + $h.Substring(0, 8) + "-" + $h.Substring(8, 4) + "-" + $h.Substring(12, 4) + "-" + $h.Substring(16, 4) + "-" + $h.Substring(20, 12) + "}" }

$product = "GRIDE"            # the name shown everywhere
$codeName = "GRIDEx2"         # the name the fixed codes are made from (ProductCode, UpgradeCode, components)
$productName = "$product $Version"
$manufacturer = "G. R. Akhtar"
$description = "GRIDE - an IDE for RIDE's toolchain, for Windows and the TMS320C6747, in a Black or a Grey outlook"
$productCode = Get-HashGuid "product|$codeName|$Version"
$packageCode = "{" + [guid]::NewGuid().ToString().ToUpperInvariant() + "}"
$upgradeCode = Get-HashGuid "upgrade|$codeName"
$ides = @(
    [pscustomobject]@{ Feature = "Black"; Folder = "Black"; Source = "GRIDEd";    Title = "GRIDE - Black outlook"; Shortcut = "GRIDE"; Level = 1
                       Description = "GRIDE in the Black outlook, with the eleven tools beside it" }
    [pscustomobject]@{ Feature = "Grey";  Folder = "Grey";  Source = "GRIDE 2.0"; Title = "GRIDE - Grey outlook";  Shortcut = "GRIDE";  Level = 3
                       Description = "GRIDE in the Grey outlook, with the eleven tools beside it" })
# White: RIDE 5.17 from GRIDE 1.0 (RIDE - The Simplest), laid out as RIDE's own stage.cmd lays it - bin\ with
# RIDE.exe, RIDEConsole.exe and the tools, include\, lib\, programs\, projects\, help\, settings.json. Off
# unless chosen (IDE=White).
$white = [pscustomobject]@{ Feature = "White"; Folder = "White"; Source = "GRIDE 1.0"; Title = "GRIDE - White outlook (RIDE - The Simplest)"
                            Shortcut = "RIDE - The Simplest"; Level = 3; Exe = "White/bin/RIDE.exe"; WorkDir = "White/bin"
                            Description = "RIDE 5.17, the simplest outlook: its window and console editor, with the eleven tools beside it" }
foreach ($ide in $ides) { $ide | Add-Member Exe "$($ide.Folder)/GRIDE.exe"; $ide | Add-Member WorkDir $ide.Folder }
$outlooks = @($ides) + $white

# ---- the stage: <stage>\GRIDEd and <stage>\GRIDE 2.0, each the solution's x64\Release ----------------
$stage = Join-Path $OutDir "stage\$product-$Version"
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
if (-not $CrtFolder) {
    $crtRoot = Get-ChildItem "$VsRoot\VC\Redist\MSVC" -Directory | Where-Object { $_.Name -match '^14\.' } | Sort-Object { [version]$_.Name }
    $crt = $crtRoot | ForEach-Object { Join-Path $_.FullName "x64\Microsoft.VC143.CRT" } | Where-Object { Test-Path $_ } | Select-Object -Last 1
    if (-not $crt) { throw "no x64\Microsoft.VC143.CRT under $VsRoot\VC\Redist\MSVC" }
    $CrtFolder = $crt
}
# what a build leaves beside the program besides the program: debug databases, and the intermediates a project
# built on its own (not through its solution) writes into its OutDir - objects, .tlog folders, recipes, caches
$skip = [regex]'(\.(pdb|ilk|metagen|iobj|ipdb|exp|obj|recipe|cache|lastbuildstate)|\.tlog|\.FileListAbsolute\.txt|AssemblyAttributes\.cpp)$|[\\/][^\\/]+\.tlog[\\/]'
Say "[1/5] Stage: $stage"
foreach ($ide in $ides) {
    $rel = Join-Path $root "$($ide.Source)\x64\Release"
    if (-not (Test-Path "$rel\GRIDE.exe")) { throw "$rel has no GRIDE.exe - build $($ide.Source)\GRIDE.sln in Release | x64 first" }
    foreach ($t in "c90", "cpp11", "shalimar", "c2s", "masm", "link", "asm6x", "lnk6x", "sim6747", "vm6747") {
        if (-not (Test-Path "$rel\$t.exe")) { throw "$rel has no $t.exe - the solution builds all eleven tools" }
    }
    $dest = Join-Path $stage $ide.Folder
    New-Item -ItemType Directory -Force $dest | Out-Null
    Get-ChildItem -LiteralPath $rel -Recurse -File | Where-Object { -not $skip.IsMatch($_.Name) -and -not $skip.IsMatch($_.FullName) -and
                                                                   $_.FullName.Substring($rel.Length + 1) -notmatch '^docs\\' } | ForEach-Object {
        $to = Join-Path $dest $_.FullName.Substring($rel.Length + 1)
        New-Item -ItemType Directory -Force (Split-Path $to) | Out-Null
        Copy-Item -LiteralPath $_.FullName -Destination $to
    }
    foreach ($dll in Get-ChildItem -LiteralPath $CrtFolder -Filter *.dll) { Copy-Item -LiteralPath $dll.FullName -Destination $dest }
    # the samples, as each IDE's samples\<name> has them, without what building them leaves behind
    $exSkip = [regex]'[\\/](\.gride|\.vs|sandbox|x64|bin|obj|out)[\\/]'
    foreach ($sample in "cppparse", "shalimar", "showcase") {
        $ex = Join-Path $root "$($ide.Source)\samples\$sample"
        if (-not (Test-Path -LiteralPath $ex)) { throw "$ex is missing - the Setup ships it" }
        Get-ChildItem -LiteralPath $ex -Recurse -File | Where-Object { -not $exSkip.IsMatch($_.FullName) } | ForEach-Object {
            $to = Join-Path $dest ("examples\$sample\" + $_.FullName.Substring($ex.Length + 1))
            New-Item -ItemType Directory -Force (Split-Path $to) | Out-Null
            Copy-Item -LiteralPath $_.FullName -Destination $to
        }
    }
    $n = (Get-ChildItem -LiteralPath $dest -Recurse -File).Count
    Say ("  {0,-10} {1,-10} {2,4} files, {3:N1} MB" -f $ide.Feature, $ide.Folder, $n, ((Get-ChildItem -LiteralPath $dest -Recurse -File | Measure-Object Length -Sum).Sum / 1MB))
}
# White, staged by RIDE's own packaging\windows\stage.cmd from GRIDE 1.0's bin\ (RIDE.sln builds RIDE and the
# eleven tools there) and the shared toolchain's headers, with the VC++ runtime beside RIDE.exe
$ride = Join-Path $root $white.Source
$rideBin = Join-Path $ride "bin"
foreach ($t in "RIDE", "RIDEConsole", "c90", "cpp11", "shalimar", "c2s", "masm", "link", "asm6x", "lnk6x", "sim6747", "vm6747") {
    if (-not (Test-Path "$rideBin\$t.exe")) { throw "$rideBin has no $t.exe - build $($white.Source)\RIDE.sln in Release | x64 into bin\ first" }
}
$whiteDest = Join-Path $stage $white.Folder
$stageCmd = Join-Path $ride "packaging\windows\stage.cmd"
$cmdArgs = '/c ""' + $stageCmd + '" "' + $ride + '" "' + (Join-Path $root "Toolchain\Compilers\C++Optimize") + '" "' + $whiteDest + '" "' +
        (Join-Path $root "Toolchain\Compilers\Compiler-Ci") + '" "' + $rideBin + '""'
$p = Start-Process cmd.exe -ArgumentList $cmdArgs -WindowStyle Hidden -Wait -PassThru
if ($p.ExitCode -ne 0 -or -not (Test-Path "$whiteDest\bin\RIDE.exe")) { throw "RIDE's stage.cmd did not stage $whiteDest (exit $($p.ExitCode))" }
foreach ($dll in Get-ChildItem -LiteralPath $CrtFolder -Filter *.dll) { Copy-Item -LiteralPath $dll.FullName -Destination "$whiteDest\bin" }
# docs\ holds RIDE's reviews, audits and studies and TI's CCS reference projects - for its developers, not its users;
# help\manual.html and shalimar-language.html are GRIDE\Help's RIDE_Manual.html and Shalimar_Reference.html (White
# looks there too); guide.html and keys.html are opened by nothing in RIDE or this Setup
if (Test-Path -LiteralPath "$whiteDest\docs") { Remove-Item -LiteralPath "$whiteDest\docs" -Recurse -Force }
foreach ($page in "manual.html", "shalimar-language.html", "guide.html", "keys.html") {
    if (Test-Path -LiteralPath "$whiteDest\help\$page") { Remove-Item -LiteralPath "$whiteDest\help\$page" -Force }
}
Say ("  {0,-10} {1,-10} {2,4} files, {3:N1} MB" -f $white.Feature, $white.Folder, (Get-ChildItem -LiteralPath $whiteDest -Recurse -File).Count, ((Get-ChildItem -LiteralPath $whiteDest -Recurse -File | Measure-Object Length -Sum).Sum / 1MB))
Say "  VC++ runtime from $CrtFolder"
# GRIDE\Help: one copy of the help for all three outlooks - RIDE's manual and the Shalimar reference for every
# outlook, the chapter pages, the VM6747 reference and toolchain.json's format for Black and Grey, and each one's own
# guide (Black's User Guide, Grey's Help). The guides link to the others by name, so they share one folder.
$helpSrc = Join-Path $root "Help"
if (-not (Test-Path "$helpSrc\RIDE_Manual.html")) { throw "$helpSrc has no RIDE_Manual.html - GRIDEx3's help folder" }
$helpDest = Join-Path $stage "Help"; New-Item -ItemType Directory -Force $helpDest | Out-Null
Get-ChildItem -LiteralPath $helpSrc -File | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $helpDest }
Say ("  {0,-10} {1,-10} {2,4} files, {3:N1} MB" -f "(shared)", "Help", (Get-ChildItem -LiteralPath $helpDest -File).Count, ((Get-ChildItem -LiteralPath $helpDest -File | Measure-Object Length -Sum).Sum / 1MB))
$helpFeatures = @{ "GRIDE_User_Guide.html" = @("Black"); "GRIDE_Help.html" = @("Grey");
                   "RIDE_Manual.html" = @("Black", "Grey", "White"); "Shalimar_Reference.html" = @("Black", "Grey", "White") }

# ---- short names, the tree, components and files (as build-msi.ps1) ----------------------------
$script:shortUsed = @{}
function Get-ShortName([string]$dirKey, [string]$long) {
    $valid = '^[A-Za-z0-9_\-!#$%&''@^`{}~]{1,8}(\.[A-Za-z0-9_\-!#$%&''@^`{}~]{1,3})?$'
    if ($long -match $valid -and $long -notmatch '\.$') { return $long }
    $dot = $long.LastIndexOf('.')
    $stem = if ($dot -ge 0) { $long.Substring(0, $dot) } else { $long }
    $ext = if ($dot -ge 0) { $long.Substring($dot + 1) } else { "" }
    $stem = ($stem -replace '[^A-Za-z0-9_\-]', '').ToUpperInvariant()
    $ext = ($ext -replace '[^A-Za-z0-9_\-]', '').ToUpperInvariant()
    if ($stem.Length -gt 6) { $stem = $stem.Substring(0, 6) }
    if ($ext.Length -gt 3) { $ext = $ext.Substring(0, 3) }
    for ($n = 1; ; $n++) {
        $candidate = $stem + "~" + $n
        if ($ext) { $candidate += "." + $ext }
        $key = "$dirKey|$($candidate.ToUpperInvariant())"
        if (-not $script:shortUsed.ContainsKey($key)) { $script:shortUsed[$key] = $true; return $candidate + "|" + $long }
    }
}
$files = @(Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object { $_.FullName.ToLowerInvariant() } | ForEach-Object {
    $rel = $_.FullName.Substring($stage.Length + 1).Replace('\', '/')
    $dir = $rel.Substring(0, $rel.LastIndexOf('/'))
    [pscustomobject]@{ Rel = $rel; Dir = $dir; Name = $_.Name; Full = $_.FullName; Size = [int]$_.Length; Top = $rel.Substring(0, $rel.IndexOf('/')) }
})
$dirKeys = @{ "" = "INSTALLDIR" }
function Get-DirKey([string]$rel) {
    if ($dirKeys.ContainsKey($rel)) { return $dirKeys[$rel] }
    $k = "D_" + (Get-HashHex "dir|$($rel.ToLowerInvariant())")
    $dirKeys[$rel] = $k
    $k
}
$dirRows = New-Object System.Collections.Generic.List[object]
$seenDirs = @{}
foreach ($f in $files) {
    $path = ""
    foreach ($p in @($f.Dir -split '/' | Where-Object { $_ -ne "" })) {
        $parent = $path
        $path = if ($path) { "$path/$p" } else { $p }
        if ($seenDirs.ContainsKey($path)) { continue }
        $seenDirs[$path] = $true
        $dirRows.Add(@((Get-DirKey $path), (Get-DirKey $parent), (Get-ShortName (Get-DirKey $parent) $p)))
    }
}
$featureOf = @{}; foreach ($ide in $outlooks) { $featureOf[$ide.Folder] = $ide.Feature }
$components = New-Object System.Collections.Generic.List[object]
$fileRows = New-Object System.Collections.Generic.List[object]
$hashRows = New-Object System.Collections.Generic.List[object]
$inst = New-MsiInstaller
$seq = 0
$fileKeyOf = @{}
foreach ($f in $files) {
    $seq++
    $h = Get-HashHex "file|$($f.Rel.ToLowerInvariant())"
    $fileKey = "F_$h"; $compKey = "C_$h"
    $fileKeyOf[$f.Rel] = $fileKey
    $features = if ($f.Top -eq "Help") { if ($helpFeatures.ContainsKey($f.Name)) { $helpFeatures[$f.Name] } else { @("Black", "Grey") } } else { @($featureOf[$f.Top]) }
    $components.Add([pscustomobject]@{ Row = @($compKey, (Get-HashGuid "component|$codeName|$($f.Rel.ToLowerInvariant())"), (Get-DirKey $f.Dir), 256, $null, $fileKey); Feature = $features })
    $fileVer = $null; $language = $null
    try { $fileVer = [string](Invoke-Msi $inst "FileVersion" @($f.Full, $false)) } catch { $fileVer = "" }
    if ($fileVer) {
        try { $language = [string](Invoke-Msi $inst "FileVersion" @($f.Full, $true)) } catch { $language = "" }
        if (-not $language) { $language = $null }
    } else {
        $fileVer = $null
        $rec = Invoke-Msi $inst "FileHash" @($f.Full, 0)
        $parts = @(1..4 | ForEach-Object { [int](Get-MsiProp $rec "IntegerData" @([int]$_)) })
        $hashRows.Add(@($fileKey, 0, $parts[0], $parts[1], $parts[2], $parts[3]))
    }
    $fileRows.Add(@($fileKey, $compKey, (Get-ShortName (Get-DirKey $f.Dir) $f.Name), $f.Size, $fileVer, $language, 512, $seq))
}

# ---- the cabinet --------------------------------------------------------------------------------
$work = Join-Path $OutDir "tmp\msi-$Version"
if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force }
New-Item -ItemType Directory -Force $work | Out-Null
$cabName = "$product.cab"
Say "[2/5] Cabinet: $($files.Count) files"
$lines = @(".OPTION EXPLICIT", ".Set CabinetNameTemplate=$cabName", ".Set DiskDirectoryTemplate=$work", ".Set Cabinet=on", ".Set Compress=on",
    ".Set CompressionType=LZX", ".Set CompressionMemory=21", ".Set UniqueFiles=ON", ".Set CabinetFileCountThreshold=0", ".Set FolderFileCountThreshold=0",
    ".Set FolderSizeThreshold=0", ".Set MaxCabinetSize=0", ".Set MaxDiskFileCount=0", ".Set MaxDiskSize=0", ".Set InfFileName=nul", ".Set RptFileName=nul")
foreach ($f in $files) { $lines += ('"' + $f.Full + '" ' + $fileKeyOf[$f.Rel]) }
[IO.File]::WriteAllLines("$work\files.ddf", $lines, (New-Object System.Text.UTF8Encoding $false))
& makecab.exe /F "$work\files.ddf" > "$OutDir\logs\makecab-$Version.log" 2>&1
if ($LASTEXITCODE -ne 0 -or -not (Test-Path "$work\$cabName")) { Get-Content "$OutDir\logs\makecab-$Version.log" | Select-Object -Last 20 | ForEach-Object { Say "  $_" }; throw "makecab failed" }
Say ("  {0}: {1:N1} MB" -f $cabName, ((Get-Item "$work\$cabName").Length / 1MB))

# ---- the database -------------------------------------------------------------------------------
$msiPath = Join-Path $OutDir "$product-Setup-$Version.msi"
Say "[3/5] Database: $msiPath"
if (Test-Path -LiteralPath $msiPath) { Remove-Item -LiteralPath $msiPath -Force }
$db = Open-MsiDatabase $inst $msiPath 3
foreach ($t in $MsiSchema.Keys) { Invoke-MsiSql $db (Get-CreateTableSql $t) }
function Row([string]$table, [object[]]$values) { Add-MsiRow $inst $db $table @($MsiSchema[$table].Columns | ForEach-Object { $_.Name }) $values }
foreach ($t in $MsiSchema.Keys) { foreach ($row in (Get-ValidationRows $t)) { Row "_Validation" $row } }

$props = [ordered]@{
    ProductCode = $productCode; ProductName = $productName; ProductVersion = $Version; Manufacturer = $manufacturer
    UpgradeCode = $upgradeCode; ProductLanguage = "1033"; ALLUSERS = "2"; ARPNOMODIFY = "1"; ARPCONTACT = $manufacturer; ARPCOMMENTS = $description
    # per user by default: with ALLUSERS=2 and no MSIINSTALLPERUSER the screens install for all users, which
    # needs an administrator the package (UAC-compliant, no elevation asked) never has - error 1925, nothing
    # installed. msiexec ... MSIINSTALLPERUSER="" ALLUSERS=1 from an elevated prompt still installs for all.
    MSIINSTALLPERUSER = "1"
    IDE = "Black"
    SecureCustomProperties = "NETFRAMEWORK48;NEWERPRODUCTFOUND;PREVIOUSVERSIONSINSTALLED;IDE"
}
if ($NoUi) { $props["LIMITUI"] = "1" }
foreach ($k in $props.Keys) { Row "Property" @($k, $props[$k]) }

Row "Directory" @("TARGETDIR", $null, "SourceDir")
Row "Directory" @("ProgramFiles64Folder", "TARGETDIR", "PFiles64")
Row "Directory" @("INSTALLDIR", "ProgramFiles64Folder", (Get-ShortName "ProgramFiles64Folder" $product))
Row "Directory" @("ProgramMenuFolder", "TARGETDIR", "PMenu")
Row "Directory" @("MENUDIR", "ProgramMenuFolder", (Get-ShortName "ProgramMenuFolder" $product))
foreach ($r in $dirRows) { Row "Directory" $r }

# Features: Black installs by default; Grey and White are level 3 (above INSTALLLEVEL 1), so only when chosen.
# Attributes 8: no advertising. On a quiet install's command line IDE=Grey or IDE=White takes that one instead
# of Black (Condition); screen 2 does the same with its events. One outlook at a time.
$order = 0
foreach ($ide in $outlooks) { $order += 2; Row "Feature" @($ide.Feature, $null, $ide.Title, $ide.Description, $order, $ide.Level, "INSTALLDIR", 8) }
Row "Condition" @("Black", 3, "IDE=`"Grey`" OR IDE=`"White`"")
Row "Condition" @("Grey", 1, "IDE=`"Grey`"")
Row "Condition" @("White", 1, "IDE=`"White`"")
foreach ($c in $components) { Row "Component" $c.Row; foreach ($ft in $c.Feature) { Row "FeatureComponents" @($ft, $c.Row[0]) } }
foreach ($r in $fileRows) { Row "File" $r }
foreach ($r in $hashRows) { Row "MsiFileHash" $r }
Row "Media" @(1, $seq, $null, "#$cabName", $null, $null)

# Shortcuts: one component per IDE in its feature, keyed by a registry value (HKCU per-user, HKLM
# per-machine), each removing the Start menu folder when it is the last one there.
$regKey = "Software\$manufacturer\$productName"
foreach ($ide in $outlooks) {
    $f = $ide.Feature
    Row "Component" @("C_Shortcut_$f", (Get-HashGuid "component|$codeName|shortcut|$f"), "MENUDIR", 260, $null, "R_InstallDir_$f")
    Row "FeatureComponents" @($f, "C_Shortcut_$f")
    Row "Registry" @("R_InstallDir_$f", -1, "$regKey\$f", "InstallDir", "[$(Get-DirKey $ide.Folder)]", "C_Shortcut_$f")
    Row "Registry" @("R_Version_$f", -1, "$regKey\$f", "Version", "[ProductVersion]", "C_Shortcut_$f")
    Row "RemoveFile" @("RF_MenuDir_$f", "C_Shortcut_$f", $null, "MENUDIR", 2)
    $exe = $ide.Exe
    if (-not $fileKeyOf.ContainsKey($exe)) { throw "the stage has no $exe" }
    $tip = if ($f -eq "White") { "RIDE - The Simplest, the IDE RIDE's toolchain began with" } else { "GRIDE, the IDE for RIDE's toolchain" }
    Row "Shortcut" @("S_$f", "MENUDIR", (Get-ShortName "MENUDIR" $ide.Shortcut), "C_Shortcut_$f", "[#$($fileKeyOf[$exe])]", $null, $tip, $null, $null, $null, 1, (Get-DirKey $ide.WorkDir))
}

Row "Upgrade" @($upgradeCode, $null, $Version, $null, 0, $null, "PREVIOUSVERSIONSINSTALLED")
Row "Upgrade" @($upgradeCode, $Version, $null, $null, 258, $null, "NEWERPRODUCTFOUND")
Row "AppSearch" @("NETFRAMEWORK48", "NetFx48Release")
Row "RegLocator" @("NetFx48Release", 2, "SOFTWARE\Microsoft\NET Framework Setup\NDP\v4\Full", "Release", 18)
Row "LaunchCondition" @("VersionNT64", "$product is a 64-bit program and needs 64-bit Windows.")
Row "LaunchCondition" @("Installed OR NOT NEWERPRODUCTFOUND", "A newer version of $product is already installed. Remove it first to install $productName.")
Row "LaunchCondition" @("Installed OR NETFRAMEWORK48 >= `"#528040`"", "$product needs the .NET Framework 4.8 or later. Install it from Microsoft and run this setup again.")
Row "CustomAction" @("SetArpInstallLocation", 51, "ARPINSTALLLOCATION", "[INSTALLDIR]")

$execute = @(
    @("FindRelatedProducts", $null, 25), @("AppSearch", $null, 50), @("LaunchConditions", $null, 100), @("ValidateProductID", $null, 700),
    @("CostInitialize", $null, 800), @("FileCost", $null, 900), @("CostFinalize", $null, 1000), @("SetArpInstallLocation", $null, 1001), @("InstallValidate", $null, 1400),
    @("InstallInitialize", $null, 1500), @("RemoveExistingProducts", $null, 1501), @("ProcessComponents", $null, 1600),
    @("UnpublishFeatures", $null, 1800), @("RemoveRegistryValues", $null, 2600), @("RemoveShortcuts", $null, 3200),
    @("RemoveFiles", $null, 3500), @("RemoveFolders", $null, 3600), @("CreateFolders", $null, 3700), @("InstallFiles", $null, 4000),
    @("CreateShortcuts", $null, 4500), @("WriteRegistryValues", $null, 5000), @("RegisterUser", $null, 6000), @("RegisterProduct", $null, 6100),
    @("PublishFeatures", $null, 6300), @("PublishProduct", $null, 6400), @("InstallFinalize", $null, 6600))
foreach ($r in $execute) { Row "InstallExecuteSequence" $r }
foreach ($r in @(@("FindRelatedProducts", $null, 25), @("AppSearch", $null, 50), @("LaunchConditions", $null, 100), @("ValidateProductID", $null, 700),
        @("CostInitialize", $null, 800), @("FileCost", $null, 900), @("CostFinalize", $null, 1000), @("SetArpInstallLocation", $null, 1001), @("ExecuteAction", $null, 1300))) { Row "InstallUISequence" $r }
foreach ($r in @(@("CostInitialize", $null, 800), @("FileCost", $null, 900), @("CostFinalize", $null, 1000), @("InstallValidate", $null, 1400),
        @("InstallInitialize", $null, 1500), @("InstallAdminPackage", $null, 3900), @("InstallFiles", $null, 4000), @("InstallFinalize", $null, 6600))) { Row "AdminExecuteSequence" $r }
foreach ($r in @(@("CostInitialize", $null, 800), @("FileCost", $null, 900), @("CostFinalize", $null, 1000), @("ExecuteAction", $null, 1300))) { Row "AdminUISequence" $r }
foreach ($r in @(@("CostInitialize", $null, 800), @("CostFinalize", $null, 1000), @("InstallValidate", $null, 1400), @("InstallInitialize", $null, 1500),
        @("CreateShortcuts", $null, 4500), @("RegisterProduct", $null, 6100), @("PublishFeatures", $null, 6300), @("PublishProduct", $null, 6400),
        @("InstallFinalize", $null, 6600))) { Row "AdvtExecuteSequence" $r }
if (-not $NoUi) { Add-SetupUi $productName }

$v = Invoke-Msi $db "OpenView" @("INSERT INTO ``_Streams`` (``Name``, ``Data``) VALUES (?, ?)")
$rec = Invoke-Msi $inst "CreateRecord" @(2)
Set-MsiProp $rec "StringData" @(1, $cabName)
Invoke-Msi $rec "SetStream" @(2, "$work\$cabName") | Out-Null
Invoke-Msi $v "Execute" @($rec) | Out-Null
Invoke-Msi $v "Close" @() | Out-Null

Say "[4/5] Summary information and commit"
$si = Get-MsiProp $db "SummaryInformation" @(20)
$now = Get-Date
foreach ($pair in @(@(1, 1252), @(2, "Installation Database"), @(3, $productName), @(4, $manufacturer), @(5, "Installer,MSI,Database,$product"),
        @(6, $description), @(7, "x64;1033"), @(9, $packageCode), @(12, $now), @(13, $now), @(14, 200), @(15, 10), @(18, "Windows Installer"), @(19, 2))) {
    Set-MsiProp $si "Property" @([int]$pair[0], $pair[1])
}
Invoke-Msi $si "Persist" @() | Out-Null
Invoke-Msi $db "Commit" @() | Out-Null
$db = $null; $si = $null; $v = $null; $rec = $null
[GC]::Collect(); [GC]::WaitForPendingFinalizers()

Say "[5/5] Read back"
$check = Open-MsiDatabase $inst $msiPath 0
$nFiles = @(Read-MsiRows $check "SELECT ``File`` FROM ``File``").Count
$nComps = @(Read-MsiRows $check "SELECT ``Component`` FROM ``Component``").Count
$nTables = @(Read-MsiRows $check "SELECT ``Name`` FROM ``_Tables``").Count
$check = $null
[GC]::Collect(); [GC]::WaitForPendingFinalizers()
Remove-Item -LiteralPath $work -Recurse -Force
Say "  $msiPath"
Say "  $nTables tables, $nFiles files in $nComps components; ProductCode $productCode, UpgradeCode $upgradeCode"
Say ("  {0:N1} MB" -f ((Get-Item -LiteralPath $msiPath).Length / 1MB))
