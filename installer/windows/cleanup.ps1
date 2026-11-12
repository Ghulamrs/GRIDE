# cleanup.ps1 - what an uninstall of GRIDE takes away besides the files the package installed, so that nothing of
# this installation is left to meet the next one. build-setup.ps1 packs it as <install folder>\cleanup.ps1, in each
# outlook's feature, and the RemoveLeftovers custom action runs it on a full removal (REMOVE="ALL", which a
# major upgrade's removal of the version before is too), just before Windows Installer removes its own files:
#
#   cleanup.ps1 "[INSTALLDIR]." "[AppDataFolder]." "[TempFolder]."
#
# (each folder ends in "\." - a folder ending in "\" before the closing quote would escape the quote). It removes
#   - the three outlook folders, Black, Grey and White, whole: what the IDE and the user's builds wrote in them
#     (a settings.json beside GRIDE.exe, the examples' build\, obj\ and out\, programs built there) as well as
#     the installed files;
#   - %APPDATA%\GRIDE, the one settings folder of the three outlooks (settings.json, a toolchain.json of the user's);
#   - %USERPROFILE%\GRIDE 1.0, where the White outlook kept its settings before;
#   - %USERPROFILE%\.gride\state.json, the White outlook's own state - the recent projects and files, the project and
#     configuration opened last - and the folder with it when nothing else is in it (1.6.2);
#   - GRIDE's own files in %TEMP%, by their exact names: GRIDE.log and GRIDE-fault.log (and their .old),
#     gride-parts-<pid>, gride-ccs-<project> and ride-ccs-<project> - no other log there, whatever its name;
#   - the install folder itself, when nothing else is left in it (a folder the user chose that holds other things stays).
# The user's own projects elsewhere (Documents\GRIDE Projects, any folder they opened) are never touched.
# Anything that cannot be removed is passed over: an uninstall never fails here.
param([string]$InstallDir = "", [string]$AppData = "", [string]$Temp = "")
$ErrorActionPreference = "SilentlyContinue"
function Full([string]$p) { if ($p) { [IO.Path]::GetFullPath($p).TrimEnd('\') } else { "" } }
$InstallDir = Full $InstallDir; $AppData = Full $AppData; $Temp = Full $Temp
if ($InstallDir) {
    foreach ($outlook in "Black", "Grey", "White") { Remove-Item -LiteralPath (Join-Path $InstallDir $outlook) -Recurse -Force }
}
if ($AppData) {
    Remove-Item -LiteralPath (Join-Path $AppData "GRIDE") -Recurse -Force
    $profileDir = Split-Path (Split-Path $AppData)               # %APPDATA% is <profile>\AppData\Roaming
    if ($profileDir) {
        Remove-Item -LiteralPath (Join-Path $profileDir "GRIDE 1.0") -Recurse -Force
        # White's state folder: its two files by name, then the folder only when that leaves it empty
        $state = Join-Path $profileDir ".gride"
        foreach ($own in "state.json", "settings.json") { Remove-Item -LiteralPath (Join-Path $state $own) -Force }
        if ((Test-Path -LiteralPath $state) -and -not (Get-ChildItem -LiteralPath $state -Force)) { Remove-Item -LiteralPath $state -Force }
    }
}
if ($Temp) {
    Get-ChildItem -LiteralPath $Temp -Force | Where-Object {
        $_.Name -in "GRIDE.log", "GRIDE.log.old", "GRIDE-fault.log", "GRIDE-fault.log.old" -or
        $_.Name -like "gride-parts-*" -or $_.Name -like "gride-ccs-*" -or $_.Name -like "ride-ccs-*"
    } | Remove-Item -Recurse -Force
}
# the install folder: Windows Installer removes it after its own files when it is empty; this script is one of them
exit 0
