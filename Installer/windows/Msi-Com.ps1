# Msi-Com.ps1 - dot-sourced: Windows Installer's automation object (WindowsInstaller.Installer, in
# msi.dll) from PowerShell. Its type library is not one PowerShell can bind to directly, so every
# call goes through InvokeMember; these wrappers keep that out of the build and test scripts.
Set-StrictMode -Version 2

$script:BF = [Reflection.BindingFlags]
function Invoke-Msi($obj, [string]$method, [object[]]$params = @()) { $obj.GetType().InvokeMember($method, $script:BF::InvokeMethod, $null, $obj, $params) }
function Get-MsiProp($obj, [string]$name, [object[]]$params = @()) { $obj.GetType().InvokeMember($name, $script:BF::GetProperty, $null, $obj, $params) }
function Set-MsiProp($obj, [string]$name, [object[]]$params) { [void]$obj.GetType().InvokeMember($name, $script:BF::SetProperty, $null, $obj, $params) }

function New-MsiInstaller { New-Object -ComObject WindowsInstaller.Installer }

# modes: 0 read-only, 1 transact, 2 direct, 3 create, 4 create direct
function Open-MsiDatabase($installer, [string]$path, [int]$mode = 0) { Invoke-Msi $installer "OpenDatabase" @($path, $mode) }

function Invoke-MsiSql($db, [string]$sql) {
    try {
        $v = Invoke-Msi $db "OpenView" @($sql)
        Invoke-Msi $v "Execute" @() | Out-Null
        Invoke-Msi $v "Close" @() | Out-Null
    } catch { throw "Windows Installer refused the SQL: $sql`n$($_.Exception.Message)" }
}

# Rows of a query as arrays of strings (a stream column comes back empty).
function Read-MsiRows($db, [string]$sql) {
    $v = Invoke-Msi $db "OpenView" @($sql)
    Invoke-Msi $v "Execute" @() | Out-Null
    $rows = @()
    while ($true) {
        $r = Invoke-Msi $v "Fetch" @()
        if ($r -eq $null) { break }
        $n = Get-MsiProp $r "FieldCount"
        $vals = @()
        for ($i = 1; $i -le $n; $i++) { $vals += [string](Get-MsiProp $r "StringData" @($i)) }
        $rows += , $vals
    }
    Invoke-Msi $v "Close" @() | Out-Null
    $rows
}

function Test-MsiTable($db, [string]$table) { (Invoke-Msi $db "TablePersistent" @($table)) -eq 1 }

# One row inserted through a record, so that values with quotes and integers need no SQL quoting.
# $columns: the table's column names, in order (INSERT INTO needs them listed);
# $values: strings, integers, $null (left null) or @{ Stream = path } for a binary column.
function Add-MsiRow($installer, $db, [string]$table, [string[]]$columns, [object[]]$values) {
    if ($columns.Count -ne $values.Count) { throw "${table}: $($columns.Count) columns, $($values.Count) values" }
    $marks = @(1..$values.Count | ForEach-Object { "?" }) -join ", "
    $names = @($columns | ForEach-Object { "``$_``" }) -join ", "
    $v = Invoke-Msi $db "OpenView" @("INSERT INTO ``$table`` ($names) VALUES ($marks)")
    $rec = Invoke-Msi $installer "CreateRecord" @($values.Count)
    for ($i = 0; $i -lt $values.Count; $i++) {
        $val = $values[$i]
        if ($val -eq $null) { continue }
        if ($val -is [hashtable]) { Invoke-Msi $rec "SetStream" @(($i + 1), $val.Stream) | Out-Null }
        elseif ($val -is [int] -or $val -is [long] -or $val -is [int16]) { Set-MsiProp $rec "IntegerData" @(($i + 1), [int]$val) }
        else { Set-MsiProp $rec "StringData" @(($i + 1), [string]$val) }
    }
    try { Invoke-Msi $v "Execute" @($rec) | Out-Null }
    catch { throw "Windows Installer refused the row for ${table}: " + (($values | ForEach-Object { if ($_ -eq $null) { "<null>" } else { [string]$_ } }) -join " | ") }
    Invoke-Msi $v "Close" @() | Out-Null
}
