param(
    [string]$ReadmePath = "README.md",
    [string]$CliPath = ".\build\Release\ispcok_cli.exe"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $ReadmePath)) {
    throw "README not found: $ReadmePath"
}
if (-not (Test-Path $CliPath)) {
    throw "CLI not found: $CliPath. Build first: cmake --build build --config Release --target ispcok_cli"
}

$startMarker = "<!-- MODULE_LIST_START -->"
$endMarker = "<!-- MODULE_LIST_END -->"

$modules = & $CliPath list-modules | Where-Object { $_ -and $_.Trim().Length -gt 0 } | ForEach-Object { $_.Trim() }
if (-not $modules -or $modules.Count -eq 0) {
    throw "No modules returned by '$CliPath list-modules'"
}

$generated = @($startMarker)
$generated += $modules | ForEach-Object { "- " + [char]96 + $_ + [char]96 }
$generated += $endMarker
$generatedBlock = ($generated -join "`r`n")

$content = Get-Content -Raw -Path $ReadmePath
$pattern = "(?s)<!-- MODULE_LIST_START -->.*?<!-- MODULE_LIST_END -->"
if ($content -notmatch $pattern) {
    throw "Cannot find marker block in README. Expected $startMarker ... $endMarker"
}

$updated = [System.Text.RegularExpressions.Regex]::Replace($content, $pattern, $generatedBlock)
Set-Content -Path $ReadmePath -Value $updated -Encoding UTF8

Write-Host "Updated module list in $ReadmePath from $CliPath"
