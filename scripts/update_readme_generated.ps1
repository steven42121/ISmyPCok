param(
    [string]$ReadmePath = "README.md",
    [string]$CliPath = ".\build\Release\ispcok_cli.exe"
)

$ErrorActionPreference = "Stop"

function Update-MarkedBlock {
    param(
        [string]$Content,
        [string]$StartMarker,
        [string]$EndMarker,
        [string[]]$Items,
        [string]$Label
    )

    if (-not $Items -or $Items.Count -eq 0) {
        throw "No $Label returned by CLI"
    }

    $generated = @($StartMarker)
    $generated += $Items | ForEach-Object { "- " + [char]96 + $_ + [char]96 }
    $generated += $EndMarker
    $generatedBlock = ($generated -join "`r`n")

    $pattern = "(?s)" + [Regex]::Escape($StartMarker) + ".*?" + [Regex]::Escape($EndMarker)
    if ($Content -notmatch $pattern) {
        throw "Cannot find marker block in README. Expected $StartMarker ... $EndMarker"
    }

    return [System.Text.RegularExpressions.Regex]::Replace($Content, $pattern, $generatedBlock)
}

function Write-FileWithRetry {
    param(
        [string]$Path,
        [string]$Content,
        [int]$MaxRetries = 5,
        [int]$DelayMs = 200
    )

    for ($i = 0; $i -lt $MaxRetries; $i++) {
        try {
            Set-Content -Path $Path -Value $Content -Encoding UTF8
            return
        }
        catch {
            if ($i -eq ($MaxRetries - 1)) {
                throw
            }
            Start-Sleep -Milliseconds $DelayMs
        }
    }
}

if (-not (Test-Path $ReadmePath)) {
    throw "README not found: $ReadmePath"
}
if (-not (Test-Path $CliPath)) {
    throw "CLI not found: $CliPath. Build first: cmake --build build --config Release --target ispcok_cli"
}

$modules = & $CliPath list-modules | Where-Object { $_ -and $_.Trim().Length -gt 0 } | ForEach-Object { $_.Trim() }
$scenarios = & $CliPath list-scenarios | Where-Object { $_ -and $_.Trim().Length -gt 0 } | ForEach-Object { $_.Trim() }

$updated = Get-Content -Raw -Path $ReadmePath
$updated = Update-MarkedBlock -Content $updated -StartMarker "<!-- MODULE_LIST_START -->" -EndMarker "<!-- MODULE_LIST_END -->" -Items $modules -Label "modules"
$updated = Update-MarkedBlock -Content $updated -StartMarker "<!-- SCENARIO_LIST_START -->" -EndMarker "<!-- SCENARIO_LIST_END -->" -Items $scenarios -Label "scenarios"
Write-FileWithRetry -Path $ReadmePath -Content $updated

Write-Host "Updated README generated blocks from $CliPath"
