param(
    [string]$ReadmePath = "",
    [string[]]$ReadmePaths = @("README.zh-CN.md", "README.en.md"),
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
            $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
            [System.IO.File]::WriteAllText($Path, $Content, $utf8NoBom)
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

if (-not (Test-Path $CliPath)) {
    throw "CLI not found: $CliPath. Build first: cmake --build build --config Release --target ispcok_cli"
}

if (-not [string]::IsNullOrWhiteSpace($ReadmePath)) {
    $ReadmePaths = @($ReadmePath)
}

if (-not $ReadmePaths -or $ReadmePaths.Count -eq 0) {
    throw "No README targets specified"
}

$modules = & $CliPath list-modules | Where-Object { $_ -and $_.Trim().Length -gt 0 } | ForEach-Object { $_.Trim() }
$scenarios = & $CliPath list-scenarios | Where-Object { $_ -and $_.Trim().Length -gt 0 } | ForEach-Object { $_.Trim() }

foreach ($targetReadme in $ReadmePaths) {
    if (-not (Test-Path $targetReadme)) {
        throw "README not found: $targetReadme"
    }

    $updated = Get-Content -Raw -Path $targetReadme
    $updated = Update-MarkedBlock -Content $updated -StartMarker "<!-- MODULE_LIST_START -->" -EndMarker "<!-- MODULE_LIST_END -->" -Items $modules -Label "modules"
    $updated = Update-MarkedBlock -Content $updated -StartMarker "<!-- SCENARIO_LIST_START -->" -EndMarker "<!-- SCENARIO_LIST_END -->" -Items $scenarios -Label "scenarios"
    Write-FileWithRetry -Path $targetReadme -Content $updated
    Write-Host "Updated generated blocks in $targetReadme from $CliPath"
}
