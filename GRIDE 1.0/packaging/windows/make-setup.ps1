# make-setup.ps1 - packaging\windows\Installer.vdproj from a staged install tree (2026-10-07).
#
# The Windows installer is a Visual Studio Setup Project (Microsoft Visual Studio
# Installer Projects 2022), built by devenv.com into RIDE-<ver>.msi. A .vdproj
# lists every file it installs and has no wildcard, so the list is written here
# from the tree stage.cmd made, just before devenv builds it:
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File make-setup.ps1 <stage dir> [version]
#
# PowerShell because every Windows machine has it and the build box has no Python.
# Every key is a hash of what it names, so the same tree gives the same file but
# for the package code, which is new per build as VS makes it; a diff of two shows
# what the installer gained or lost. What is left out - the dialogs - is the
# template's default, which VS fills in when it loads the project.
param([Parameter(Mandatory = $true)][string]$Stage, [string]$Version = "5.17")
$ErrorActionPreference = "Stop"

$Product = "RIDE"
$Manufacturer = "G. R. Akhtar"
# Fixed for every 5.x: the upgrade code is what lets a later .msi replace this one.
$UpgradeCode = "{3D0B6F2E-5A41-4C8E-9B7D-52E1A6C0F5A1}"
$AppFolder = "{3C67513D-01DD-4637-8A68-80971EB9504F}"
$SubFolder = "{9EF0B969-E518-4E46-987F-47570745A589}"
$SpecialFolder = "{1525181F-901A-416C-8A58-119130FE478E}"
$FileType = "{1FB2D0AE-D3B9-43D4-B9DD-F88EC61E35DE}"
$ShortcutType = "{970C0BB2-C7D0-45D7-ABFA-7EC378858BC0}"

$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Stage = (Resolve-Path $Stage).Path.TrimEnd('\')
$Name = "$Product $Version"
if (-not (Test-Path "$Stage\bin\$Product.exe")) { throw "make-setup.ps1: $Stage has no bin\$Product.exe - stage it first" }

$md5 = [System.Security.Cryptography.MD5]::Create()
function Hex([string]$s) {
    ($md5.ComputeHash([Text.Encoding]::UTF8.GetBytes($s)) | ForEach-Object { $_.ToString("X2") }) -join ""
}
function Key([string]$s) { "_" + (Hex $s) }
function Guid([string]$s) { $h = Hex $s; "{" + $h.Substring(0, 8) + "-" + $h.Substring(8, 4) + "-" + $h.Substring(12, 4) + "-" + $h.Substring(16, 4) + "-" + $h.Substring(20, 12) + "}" }
function Esc([string]$s) { $s.Replace('\', '\\') }

$sb = New-Object System.Text.StringBuilder
function Line([int]$lDepth, [string]$lText) { [void]$sb.Append(("    " * $lDepth) + $lText + "`r`n") }
# One "name" { "field" = "value" ... } block; $bInner writes what goes after the fields. The
# parameters' names are their own: a scriptblock runs in Block's scope and would see a $name of it.
function Block([int]$bDepth, [string]$bName, $bFields, [scriptblock]$bInner = $null) {
    Line $bDepth ('"' + $bName + '"'); Line $bDepth "{"
    # The comma binds before +, so @("Name", "8:" + $x) is three items: the value is the rest joined.
    foreach ($bf in $bFields) { Line $bDepth ('"' + $bf[0] + '" = "' + ($bf[1..($bf.Count - 1)] -join "") + '"') }
    if ($bInner) { & $bInner }
    Line $bDepth "}"
}

# The tree: every file by its folder relative to the stage, "" for the stage itself.
$files = New-Object System.Collections.Generic.List[object]
$dirs = New-Object System.Collections.Generic.List[string]
Get-ChildItem -LiteralPath $Stage -Recurse -Force | Sort-Object { $_.FullName.ToLowerInvariant() } | ForEach-Object {
    $rel = $_.FullName.Substring($Stage.Length).TrimStart('\')
    if ($_.PSIsContainer) { $dirs.Add($rel) }
    else { $files.Add(@{ Dir = (Split-Path -Parent $rel); Name = $_.Name; Full = $_.FullName }) }
}
$relBase = New-Object System.Uri ($Here + '\')
function Source($full) {
    $u = New-Object System.Uri $full
    if ($u.Host -eq $relBase.Host -and $full.Substring(0, 2) -ieq $Here.Substring(0, 2)) {
        [Uri]::UnescapeDataString($relBase.MakeRelativeUri($u).ToString()).Replace('/', '\')
    } else { $full }
}
function FolderKey([string]$rel) { Key ("folder|" + $rel) }
function FileKey($f) { Key ("file|" + $f.Dir + "|" + $f.Name) }
function Folders([int]$depth, [string]$parent) {
    Line $depth '"Folders"'; Line $depth "{"
    foreach ($d in $dirs) {
        if ((Split-Path -Parent $d) -ne $parent) { continue }
        Block ($depth + 1) ($SubFolder + ":" + (FolderKey $d)) @(
            @("Name", "8:" + (Split-Path -Leaf $d)), @("AlwaysCreate", "11:FALSE"), @("Condition", "8:"),
            @("Transitive", "11:FALSE"), @("Property", "8:" + (Key ("property|" + $d)))) { Folders ($depth + 2) $d }
    }
    Line $depth "}"
}

# The shortcuts: each names a file of the tree, and the tree must have it.
$menuKey = Key "special|menu"
$desktopKey = Key "special|desktop"
$wanted = @(
    @("menu", $Name, "bin", "$Product.exe"), @("menu", "$Name (console)", "bin", "${Product}Console.exe"),
    @("menu", "$Name command prompt", "bin", "ride-prompt.cmd"), @("menu", "Express Help", "", "EXPRESS-HELP.html"),
    @("menu", "Manual", "help", "manual.html"), @("menu", "User Guide", "help", "guide.html"),
    @("menu", "Resources", "help", "resources.html"), @("desktop", $Name, "bin", "$Product.exe"))
foreach ($w in $wanted) {
    if (-not ($files | Where-Object { $_.Dir -eq $w[2] -and $_.Name -eq $w[3] })) {
        throw ("make-setup.ps1: the shortcut '" + $w[1] + "' names " + (Join-Path $w[2] $w[3]) + ", which the stage has not")
    }
}

Line 0 '"DeployProject"'; Line 0 "{"
foreach ($f in @(@("VSVersion", "3:800"), @("ProjectType", "8:{978C614F-708E-4E1A-B201-565925725DBA}"),
        @("IsWebType", "8:FALSE"),
        @("ProjectName", "8:Installer"), @("LanguageId", "3:1033"), @("CodePage", "3:1252"), @("UILanguageId", "3:1033"),
        @("SccProjectName", "8:"), @("SccLocalPath", "8:"), @("SccAuxPath", "8:"), @("SccProvider", "8:"))) {
    Line 0 ('"' + $f[0] + '" = "' + $f[1] + '"')
}
Line 1 '"Hierarchy"'; Line 1 "{"
foreach ($f in $files) { Block 2 "Entry" @(@("MsmKey", "8:" + (FileKey $f)), @("OwnerKey", "8:_UNDEFINED"), @("MsmSig", "8:_UNDEFINED")) }
Line 1 "}"
Line 1 '"Configurations"'; Line 1 "{"
foreach ($c in @(@("Debug", "TRUE", "FALSE"), @("Release", "FALSE", "TRUE"))) {
    Block 2 $c[0] @(@("DisplayName", "8:" + $c[0]), @("IsDebugOnly", "11:" + $c[1]), @("IsReleaseOnly", "11:" + $c[2]),
        @("OutputFilename", "8:" + $c[0] + "\\$Product-$Version.msi"), @("PackageFilesAs", "3:2"),
        @("PackageFileSize", "3:-2147483648"), @("CabType", "3:1"), @("Compression", "3:2"), @("SignOutput", "11:FALSE"),
        @("CertificateFile", "8:"), @("PrivateKeyFile", "8:"), @("TimeStampServer", "8:"), @("InstallerBootstrapper", "3:1"))
}
Line 1 "}"
Line 1 '"Deployable"'; Line 1 "{"
Line 2 '"File"'; Line 2 "{"
foreach ($f in $files) {
    Block 3 ($FileType + ":" + (FileKey $f)) @(@("SourcePath", "8:" + (Esc (Source $f.Full))), @("TargetName", "8:" + $f.Name),
        @("Tag", "8:"), @("Folder", "8:" + (FolderKey $f.Dir)), @("Condition", "8:"), @("Transitive", "11:FALSE"),
        @("Vital", "11:TRUE"), @("ReadOnly", "11:FALSE"), @("Hidden", "11:FALSE"), @("System", "11:FALSE"),
        @("Permanent", "11:FALSE"), @("SharedLegacy", "11:FALSE"), @("PackageAs", "3:1"), @("Register", "3:1"),
        @("Exclude", "11:FALSE"), @("IsDependency", "11:FALSE"), @("IsolateTo", "8:"))
}
Line 2 "}"
Line 2 '"Folder"'; Line 2 "{"
Block 3 ($AppFolder + ":" + (FolderKey "")) @(@("DefaultLocation", "8:[ProgramFiles64Folder][ProductName]"), @("Name", "8:#1925"),
    @("AlwaysCreate", "11:FALSE"), @("Condition", "8:"), @("Transitive", "11:FALSE"), @("Property", "8:TARGETDIR")) { Folders 4 "" }
Block 3 ($SpecialFolder + ":" + $desktopKey) @(@("Name", "8:#1916"), @("AlwaysCreate", "11:FALSE"), @("Condition", "8:"),
    @("Transitive", "11:FALSE"), @("Property", "8:DesktopFolder")) { Line 4 '"Folders"'; Line 4 "{"; Line 4 "}" }
Block 3 ($SpecialFolder + ":" + (Key "special|programs")) @(@("Name", "8:#1919"), @("AlwaysCreate", "11:FALSE"), @("Condition", "8:"),
    @("Transitive", "11:FALSE"), @("Property", "8:ProgramMenuFolder")) {
    Line 4 '"Folders"'; Line 4 "{"
    Block 5 ($SubFolder + ":" + $menuKey) @(@("Name", "8:$Name"), @("AlwaysCreate", "11:FALSE"), @("Condition", "8:"),
        @("Transitive", "11:FALSE"), @("Property", "8:" + (Key "special-property|menu"))) { Line 6 '"Folders"'; Line 6 "{"; Line 6 "}" }
    Line 4 "}"
}
Line 2 "}"
# A product code per version, so 5.17 replaces 5.1 (RemovePreviousVersions); a package code per build.
Block 2 "Product" @(@("Name", "8:Microsoft Visual Studio"), @("ProductName", "8:$Name"),
    @("ProductCode", "8:" + (Guid ("product|" + $Version))), @("PackageCode", "8:{" + ([System.Guid]::NewGuid().ToString().ToUpperInvariant()) + "}"),
    @("UpgradeCode", "8:$UpgradeCode"), @("AspNetVersion", "8:2.0.50727.0"), @("RestartWWWService", "11:FALSE"),
    @("RemovePreviousVersions", "11:TRUE"), @("DetectNewerInstalledVersion", "11:TRUE"), @("InstallAllUsers", "11:TRUE"),
    @("ProductVersion", "8:$Version.0"), @("Manufacturer", "8:$Manufacturer"), @("ARPHELPTELEPHONE", "8:"), @("ARPHELPLINK", "8:"),
    @("Title", "8:$Name"), @("Subject", "8:"), @("ARPCONTACT", "8:$Manufacturer"), @("Keywords", "8:"), @("ARPCOMMENTS", "8:"),
    @("ARPURLINFOABOUT", "8:"), @("ARPPRODUCTICON", "8:"), @("ARPIconIndex", "3:0"), @("SearchPath", "8:"),
    @("UseSystemSearchPath", "11:TRUE"), @("TargetPlatform", "3:1"), @("PreBuildEvent", "8:"), @("PostBuildEvent", "8:"),
    @("RunPostBuildEvent", "3:0"))
Line 2 '"Shortcut"'; Line 2 "{"
foreach ($w in $wanted) {
    $target = $files | Where-Object { $_.Dir -eq $w[2] -and $_.Name -eq $w[3] } | Select-Object -First 1
    Block 3 ($ShortcutType + ":" + (Key ("shortcut|" + $w[0] + "|" + $w[1]))) @(@("Name", "8:" + $w[1]), @("Arguments", "8:"),
        @("Description", "8:"), @("ShowCmd", "3:1"), @("IconIndex", "3:0"), @("Transitive", "11:FALSE"),
        @("Target", "8:" + (FileKey $target)), @("Folder", "8:" + $(if ($w[0] -eq "menu") { $menuKey } else { $desktopKey })),
        @("WorkingFolder", "8:" + (FolderKey $w[2])), @("Icon", "8:"), @("Feature", "8:"))
}
Line 2 "}"
Line 1 "}"
Line 0 "}"

$out = Join-Path $Here "Installer.vdproj"
$utf8 = New-Object System.Text.UTF8Encoding $true
[IO.File]::WriteAllText($out, $sb.ToString(), $utf8)
# devenv builds a .vdproj only through a solution, and RIDE.sln would have it build every
# project again into its own OutDir; Installer.sln is the setup project alone.
$p = "{" + ((Guid "Installer").Trim("{}")) + "}"
$sln = @("", "Microsoft Visual Studio Solution File, Format Version 12.00", "# Visual Studio Version 17",
    "VisualStudioVersion = 17.0.31903.59", "MinimumVisualStudioVersion = 10.0.40219.1",
    "Project(`"{54435603-DBB4-11D2-8724-00A0C9A8B90C}`") = `"Installer`", `"Installer.vdproj`", `"$p`"", "EndProject",
    "Global", "`tGlobalSection(SolutionConfigurationPlatforms) = preSolution", "`t`tDebug|Default = Debug|Default",
    "`t`tRelease|Default = Release|Default", "`tEndGlobalSection", "`tGlobalSection(ProjectConfigurationPlatforms) = postSolution",
    "`t`t$p.Debug|Default.ActiveCfg = Debug", "`t`t$p.Release|Default.ActiveCfg = Release",
    "`t`t$p.Release|Default.Build.0 = Release", "`tEndGlobalSection", "EndGlobal", "") -join "`r`n"
[IO.File]::WriteAllText((Join-Path $Here "Installer.sln"), $sln, $utf8)
"make-setup.ps1: $out - $($files.Count) files, $($wanted.Count) shortcuts, $Name"
