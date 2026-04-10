param(
    [string]$ReadmePath = "",
    [string]$CliPath = ".\build\Release\ispcok_cli.exe"
)

if ([string]::IsNullOrWhiteSpace($ReadmePath)) {
    & "$PSScriptRoot\update_readme_generated.ps1" -CliPath $CliPath
}
else {
    & "$PSScriptRoot\update_readme_generated.ps1" -ReadmePath $ReadmePath -CliPath $CliPath
}
