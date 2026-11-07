# Publish Windows installer to GitHub Release (OnePureLotus fork). Requires: gh auth login OR $env:GH_TOKEN
$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path $PSScriptRoot -Parent
$Gh = $null
$cmd = Get-Command gh -ErrorAction SilentlyContinue | Select-Object -First 1
if ($cmd) {
    $Gh = $cmd.Source
} else {
    $Gh = Join-Path $RepoRoot ".tools\gh\gh.exe"
}
if (-not (Test-Path -LiteralPath $Gh)) {
    throw "Missing gh.exe — install GitHub CLI or use $RepoRoot\.tools\gh\gh.exe"
}
Write-Host "Using gh: $Gh"
$Tag = "v0.1.29"
$Repo = "OnePureLotus/FluentSerialAssistant"
$Setup = Join-Path $RepoRoot "dist\FluentSerialAssistant-0.1.29-windows-x64-setup.exe"
$Sha = "$Setup.sha256"
$Notes = Join-Path $RepoRoot "docs\release-notes\v0.1.29.md"
foreach ($f in @($Setup, $Sha, $Notes)) {
    if (-not (Test-Path -LiteralPath $f)) { throw "Missing file: $f" }
}
& $Gh auth status 2>&1 | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "gh is not authenticated. In this same terminal run: gh auth login --hostname github.com --git-protocol https --web"
}
& $Gh release view $Tag --repo $Repo 2>$null
if ($LASTEXITCODE -eq 0) {
    Write-Host "Release $Tag exists; uploading assets..."
    & $Gh release upload $Tag --repo $Repo --clobber $Setup $Sha
} else {
    Write-Host "Creating release $Tag..."
    & $Gh release create $Tag `
        --repo $Repo `
        --title "FluentSerialAssistant 0.1.29" `
        --notes-file $Notes `
        --target main `
        $Setup $Sha
}
if ($LASTEXITCODE -ne 0) { throw "gh release failed with exit code $LASTEXITCODE." }
& $Gh release edit $Tag --repo $Repo --notes-file $Notes
Write-Host "Done. https://github.com/$Repo/releases/tag/$Tag"
