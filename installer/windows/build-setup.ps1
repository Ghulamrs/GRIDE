# build-setup.ps1 - GRIDE-Setup-<ver>.msi: one Windows Installer package that installs one of the three
# outlooks of GRIDEx3 - Black (GRIDEd), Grey (GRIDE 2.0) or White, The Simple (GRIDE 1.0, RIDE's
# window) - each with the toolchain beside it, chosen on the second screen, White last. What the user
# sees names only GRIDE and the outlooks (the user's rules of 2026-10-16 and 2026-10-19): the screens,
# the folders GRIDE\Black, GRIDE\Grey and GRIDE\White, the Start menu, Programs and Features. Written
# through Windows Installer's own automation API (msi.dll): no WiX, no Inno Setup, no setup project.
# Adapted from GRIDE-Installer's build-msi.ps1 (one IDE, dummy tools).
#
#   build-setup.ps1 [-Version 1.4.0] [-OutDir <GRIDEx3\installer\dist>]
#                   [-CertThumbprint <sha1> | -PfxFile <file.pfx> [-PfxPassword <SecureString>]]
#                   [-TimestampUrl <url>] [-SignTool <signtool.exe>]
#
# Signing is optional. With -CertThumbprint (a code-signing certificate in the current user's or the
# machine's store) or -PfxFile, signtool signs GRIDE.exe and the ten programs of each IDE before they
# are packed, then the .msi, SHA-256 with an RFC 3161 timestamp from -TimestampUrl. The certificate is
# the publisher's - G. R. Akhtar's, from a certification authority Windows trusts, so that Windows names
# him on the Setup's prompt; this script never makes, imports or trusts one. Without either, nothing is
# signed and Windows shows the Setup as from an unknown publisher (Programs and Features still says
# G. R. Akhtar, from the Manufacturer property).
#
# Input: each solution's Release build, as Visual Studio's Build Solution leaves it - GRIDEd\x64\Release,
# GRIDE 2.0\x64\Release and GRIDE 1.0\x64\Release (GRIDE.exe, the ten programs, docs\, include\, lib\, and
# toolchain.json for Black and Grey, settings.json for White) - and GRIDEx3\examples, the one examples folder
# all three install as <outlook>\examples: projects\ (cppparse, built with cpp11; the C, C++ and Shalimar
# projects; the CCS samples) and programs\ (single files).
# Build the three first (one at a time: they share the toolchain's build folders). Left out: .pdb, .ilk,
# .metagen. Added: Visual Studio 2022's VC++ runtime DLLs beside GRIDE.exe (app-local, as Microsoft
# allows for Microsoft.VC143.CRT), so a machine without the VC++ Redistributable runs it.
#
# The package: features Black (level 1), Grey and White (level 3, so not installed unless chosen); the
# second dialog's Install button removes the two not chosen and adds the one chosen. A quiet install
# takes IDE=Grey or IDE=White (Condition table). ALLUSERS=2 with the UAC-compliant bit:
# MSIINSTALLPERUSER=1 installs under the user's profile without elevation. Launch conditions: 64-bit
# Windows, .NET Framework 4.8, no newer GRIDE. Component GUIDs are a hash of each file's install path,
# the ProductCode a hash of the version, the UpgradeCode fixed - still made from "GRIDEx2", so this
# Setup replaces 1.1.0.
param(
    [string]$Version = "1.4.0",
    [string]$OutDir = "",
    [string]$VsRoot = "D:\Microsoft Visual Studio 2026",
    [string]$CrtFolder = "",
    [switch]$NoUi,
    [string]$CertThumbprint = "",
    [string]$PfxFile = "",
    [SecureString]$PfxPassword = $null,
    [string]$TimestampUrl = "http://timestamp.digicert.com",
    [string]$SignTool = ""
)
$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2
. (Join-Path $PSScriptRoot "msi-schema.ps1")
. (Join-Path $PSScriptRoot "setup-ui.ps1")
. (Join-Path $PSScriptRoot "Msi-Com.ps1")
if ($Version -notmatch '^\d{1,3}\.\d{1,3}\.\d{1,5}$') { throw "the version must be major.minor.build: $Version" }
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)          # GRIDEx3
if (-not $OutDir) { $OutDir = Join-Path $root "installer\dist" }
$OutDir = [IO.Path]::GetFullPath($OutDir)
New-Item -ItemType Directory -Force "$OutDir\tmp", "$OutDir\logs" | Out-Null
$env:TMP = "$OutDir\tmp"; $env:TEMP = "$OutDir\tmp"
function Say([string]$s) { Write-Output $s }

# ---- signing (optional): found and checked before anything is built ---------------------------
$CertThumbprint = $CertThumbprint -replace '[^0-9A-Fa-f]', ''
$sign = [bool]($CertThumbprint -or $PfxFile)
if ($CertThumbprint -and $PfxFile) { throw "name the certificate once: -CertThumbprint or -PfxFile" }
if ($CertThumbprint -and $CertThumbprint.Length -ne 40) { throw "-CertThumbprint is the certificate's SHA-1 thumbprint, 40 hex digits" }
if ($PfxFile -and -not (Test-Path -LiteralPath $PfxFile)) { throw "no such .pfx: $PfxFile" }
if ($sign -and -not $SignTool) {
    $SignTool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue |
        Where-Object { $_.Directory.Parent.Name -match '^\d+(\.\d+){3}$' } | Sort-Object { [version]$_.Directory.Parent.Name } |
        Select-Object -Last 1 -ExpandProperty FullName
}
if ($sign -and (-not $SignTool -or -not (Test-Path -LiteralPath $SignTool))) { throw "signtool.exe not found - install the Windows SDK's signing tools or pass -SignTool" }
function Invoke-Sign([string[]]$paths, [string]$what) {
    $ErrorActionPreference = "Continue"     # signtool's own errors go to its log, not out as PowerShell's
    $a = @("sign", "/fd", "SHA256", "/d", $what)
    if ($CertThumbprint) { $a += @("/sha1", $CertThumbprint) }
    else {
        $a += @("/f", $PfxFile)
        if ($PfxPassword) { $a += @("/p", [Net.NetworkCredential]::new("", $PfxPassword).Password) }
    }
    if ($TimestampUrl) { $a += @("/tr", $TimestampUrl, "/td", "SHA256") }
    $log = "$OutDir\logs\signtool-$Version.log"
    & $SignTool @a @paths 2>&1 | ForEach-Object { "$_" } | Add-Content -LiteralPath $log
    if ($LASTEXITCODE -ne 0) { Get-Content $log | Select-Object -Last 6 | ForEach-Object { Say "  $_" }; throw "signtool could not sign $what ($LASTEXITCODE) - see $log" }
    Say "  signed $($paths.Count) file(s): $what"
}

$script:md5 = [Security.Cryptography.MD5]::Create()
function Get-HashHex([string]$s) { ($script:md5.ComputeHash([Text.Encoding]::UTF8.GetBytes($s)) | ForEach-Object { $_.ToString("X2") }) -join "" }
function Get-HashGuid([string]$s) { $h = Get-HashHex $s; "{" + $h.Substring(0, 8) + "-" + $h.Substring(8, 4) + "-" + $h.Substring(12, 4) + "-" + $h.Substring(16, 4) + "-" + $h.Substring(20, 12) + "}" }

$product = "GRIDE"            # the name shown everywhere
$codeName = "GRIDEx2"         # the name the fixed codes are made from (ProductCode, UpgradeCode, components) - kept, so 1.1.0 is upgraded
$productName = "$product $Version"
$manufacturer = "G. R. Akhtar"
$description = "GRIDE - an IDE for RIDE's toolchain, for Windows and the TMS320C6747, in a Black, a Grey or a White outlook"
$productCode = Get-HashGuid "product|$codeName|$Version"
$packageCode = "{" + [guid]::NewGuid().ToString().ToUpperInvariant() + "}"
$upgradeCode = Get-HashGuid "upgrade|$codeName"
$ides = @(
    [pscustomobject]@{ Feature = "Black"; Folder = "Black"; Source = "GRIDEd";    Title = "GRIDE - Black outlook"; Shortcut = "GRIDE"; Level = 1
                       Description = "GRIDE in the Black outlook, with the eleven tools beside it" }
    [pscustomobject]@{ Feature = "Grey";  Folder = "Grey";  Source = "GRIDE 2.0"; Title = "GRIDE - Grey outlook";  Shortcut = "GRIDE";  Level = 3
                       Description = "GRIDE in the Grey outlook, with the eleven tools beside it" }
    [pscustomobject]@{ Feature = "White"; Folder = "White"; Source = "GRIDE 1.0"; Title = "GRIDE - White outlook, The Simple"; Shortcut = "GRIDE"; Level = 3
                       Description = "GRIDE in the White outlook, The Simple - RIDE's window - with the eleven tools beside it" })

# ---- the stage: <stage>\Black, <stage>\Grey and <stage>\White, each the solution's x64\Release --------
$stage = Join-Path $OutDir "stage\$product-$Version"
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
if (-not $CrtFolder) {
    $crtRoot = Get-ChildItem "$VsRoot\VC\Redist\MSVC" -Directory | Where-Object { $_.Name -match '^14\.' } | Sort-Object { [version]$_.Name }
    $crt = $crtRoot | ForEach-Object { Join-Path $_.FullName "x64\Microsoft.VC143.CRT" } | Where-Object { Test-Path $_ } | Select-Object -Last 1
    if (-not $crt) { throw "no x64\Microsoft.VC143.CRT under $VsRoot\VC\Redist\MSVC" }
    $CrtFolder = $crt
}
$skip = [regex]'\.(pdb|ilk|metagen|iobj|ipdb|exp)$'
Say "[1/5] Stage: $stage"
foreach ($ide in $ides) {
    $rel = Join-Path $root "$($ide.Source)\x64\Release"
    if (-not (Test-Path "$rel\GRIDE.exe")) { throw "$rel has no GRIDE.exe - build $($ide.Source)\GRIDE.sln in Release | x64 first" }
    foreach ($t in "c90", "cpp11", "shalimar", "c2s", "masm", "link", "asm6x", "lnk6x", "sim6747", "vm6747") {
        if (-not (Test-Path "$rel\$t.exe")) { throw "$rel has no $t.exe - the solution builds all eleven tools" }
    }
    $dest = Join-Path $stage $ide.Folder
    New-Item -ItemType Directory -Force $dest | Out-Null
    Get-ChildItem -LiteralPath $rel -Recurse -File | Where-Object { -not $skip.IsMatch($_.Name) } | ForEach-Object {
        $to = Join-Path $dest $_.FullName.Substring($rel.Length + 1)
        New-Item -ItemType Directory -Force (Split-Path $to) | Out-Null
        Copy-Item -LiteralPath $_.FullName -Destination $to
    }
    foreach ($dll in Get-ChildItem -LiteralPath $CrtFolder -Filter *.dll) { Copy-Item -LiteralPath $dll.FullName -Destination $dest }
    # One examples folder for the three, GRIDEx3\examples; anything a build or an IDE left in it stays out.
    $ex = Join-Path $root "examples"
    # (the toolchain demo's out\ is what a run of demo.ps1 left: its linker command file and .shl too)
    $exSkip = [regex]'([\\/](\.gride|\.vs|sandbox|x64|bin|obj|build)[\\/])|([\\/]toolchain-demo[\\/]out[\\/])|\.(s|obj|o|exe|out|map|pdb|ilk|user)$'
    Get-ChildItem -LiteralPath $ex -Recurse -File | Where-Object { -not $exSkip.IsMatch($_.FullName.Substring($ex.Length)) } | ForEach-Object {
        $to = Join-Path $dest ("examples\" + $_.FullName.Substring($ex.Length + 1))
        New-Item -ItemType Directory -Force (Split-Path $to) | Out-Null
        Copy-Item -LiteralPath $_.FullName -Destination $to
    }
    $n = (Get-ChildItem -LiteralPath $dest -Recurse -File).Count
    Say ("  {0,-10} {1,-10} {2,4} files, {3:N1} MB" -f $ide.Feature, $ide.Folder, $n, ((Get-ChildItem -LiteralPath $dest -Recurse -File | Measure-Object Length -Sum).Sum / 1MB))
}
Say "  VC++ runtime from $CrtFolder"
if ($sign) {
    # GRIDE's own programs only: the VC++ runtime DLLs carry Microsoft's signature already
    $own = foreach ($ide in $ides) { foreach ($t in "GRIDE", "c90", "cpp11", "shalimar", "c2s", "masm", "link", "asm6x", "lnk6x", "sim6747", "vm6747") { Join-Path $stage "$($ide.Folder)\$t.exe" } }
    Invoke-Sign @($own) "GRIDE"
}

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
$featureOf = @{}; foreach ($ide in $ides) { $featureOf[$ide.Folder] = $ide.Feature }
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
    $components.Add([pscustomobject]@{ Row = @($compKey, (Get-HashGuid "component|$codeName|$($f.Rel.ToLowerInvariant())"), (Get-DirKey $f.Dir), 256, $null, $fileKey); Feature = $featureOf[$f.Top] })
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

# Features: Black installs by default; Grey and White are level 3 (above INSTALLLEVEL 1), so only when
# chosen. Attributes 8: no advertising. IDE=Grey or IDE=White on a quiet install's command line puts
# that one in Black's place (Condition).
$order = 0
foreach ($ide in $ides) { $order += 2; Row "Feature" @($ide.Feature, $null, $ide.Title, $ide.Description, $order, $ide.Level, "INSTALLDIR", 8) }
Row "Condition" @("Black", 3, "IDE=`"Grey`" OR IDE=`"White`"")
Row "Condition" @("Grey", 1, "IDE=`"Grey`"")
Row "Condition" @("White", 1, "IDE=`"White`"")
foreach ($c in $components) { Row "Component" $c.Row; Row "FeatureComponents" @($c.Feature, $c.Row[0]) }
foreach ($r in $fileRows) { Row "File" $r }
foreach ($r in $hashRows) { Row "MsiFileHash" $r }
Row "Media" @(1, $seq, $null, "#$cabName", $null, $null)

# Shortcuts: one component per IDE in its feature, keyed by a registry value (HKCU per-user, HKLM
# per-machine), each removing the Start menu folder when it is the last one there.
$regKey = "Software\$manufacturer\$productName"
foreach ($ide in $ides) {
    $f = $ide.Feature
    Row "Component" @("C_Shortcut_$f", (Get-HashGuid "component|$codeName|shortcut|$f"), "MENUDIR", 260, $null, "R_InstallDir_$f")
    Row "FeatureComponents" @($f, "C_Shortcut_$f")
    Row "Registry" @("R_InstallDir_$f", -1, "$regKey\$f", "InstallDir", "[$(Get-DirKey $ide.Folder)]", "C_Shortcut_$f")
    Row "Registry" @("R_Version_$f", -1, "$regKey\$f", "Version", "[ProductVersion]", "C_Shortcut_$f")
    Row "RemoveFile" @("RF_MenuDir_$f", "C_Shortcut_$f", $null, "MENUDIR", 2)
    $exe = "$($ide.Folder)/GRIDE.exe"
    if (-not $fileKeyOf.ContainsKey($exe)) { throw "the stage has no $exe" }
    Row "Shortcut" @("S_$f", "MENUDIR", (Get-ShortName "MENUDIR" $ide.Shortcut), "C_Shortcut_$f", "[#$($fileKeyOf[$exe])]", $null, "GRIDE, the IDE for RIDE's toolchain", $null, $null, $null, 1, (Get-DirKey $ide.Folder))
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
if ($sign) {
    Invoke-Sign @($msiPath) $productName
    & { $ErrorActionPreference = "Continue"; & $SignTool verify /pa /q $msiPath *> $null }
    if ($LASTEXITCODE -eq 0) { Say "  signature verified (signtool verify /pa)" }
    else { Say "  WARNING: signtool verify /pa does not accept the signature - Windows does not trust the certificate's issuer here" }
}
Say "  $msiPath"
Say "  $nTables tables, $nFiles files in $nComps components; ProductCode $productCode, UpgradeCode $upgradeCode"
Say ("  {0:N1} MB" -f ((Get-Item -LiteralPath $msiPath).Length / 1MB))
