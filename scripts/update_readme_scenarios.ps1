param(
    [string]$ReadmePath = "README.md",
    [string]$CliPath = ".\build\Release\ispcok_cli.exe"
)

& "$PSScriptRoot\update_readme_generated.ps1" -ReadmePath $ReadmePath -CliPath $CliPath
