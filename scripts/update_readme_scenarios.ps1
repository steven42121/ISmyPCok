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

$startMarker = "<!-- SCENARIO_LIST_START -->"
$endMarker = "<!-- SCENARIO_LIST_END -->"

$scenarios = & $CliPath list-scenarios | Where-Object { $_ -and $_.Trim().Length -gt 0 } | ForEach-Object { $_.Trim() }
if (-not $scenarios -or $scenarios.Count -eq 0) {
    throw "No scenarios returned by '$CliPath list-scenarios'"
}

$generated = @($startMarker)
$generated += $scenarios | ForEach-Object { "- " + [char]96 + $_ + [char]96 }
$generated += $endMarker
$generatedBlock = ($generated -join "`r`n")

$content = Get-Content -Raw -Path $ReadmePath
$pattern = "(?s)<!-- SCENARIO_LIST_START -->.*?<!-- SCENARIO_LIST_END -->"
if ($content -notmatch $pattern) {
    throw "Cannot find marker block in README. Expected $startMarker ... $endMarker"
}

$updated = [System.Text.RegularExpressions.Regex]::Replace($content, $pattern, $generatedBlock)
Set-Content -Path $ReadmePath -Value $updated -Encoding UTF8

Write-Host "Updated scenario list in $ReadmePath from $CliPath"
