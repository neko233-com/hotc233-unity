param(
    [string]$InstallPath = "Assets/neko233/hotc233-unity",
    [string]$RepoUrl = "https://github.com/neko233-com/hotc233-unity.git",
    [switch]$Submodule
)

$ErrorActionPreference = "Stop"

function Invoke-Git {
    param(
        [string[]]$Arguments,
        [string]$WorkingDirectory = (Get-Location).Path
    )

    & git @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "git $($Arguments -join ' ') failed with exit code $LASTEXITCODE."
    }
}

if (-not (Test-Path "Assets")) {
    throw "Run this command from the Unity project root. The Assets directory was not found."
}

if ($env:HOTC233_INSTALL_PATH) {
    $InstallPath = $env:HOTC233_INSTALL_PATH
}

if ($env:HOTC233_REPO_URL) {
    $RepoUrl = $env:HOTC233_REPO_URL
}

if ($env:HOTC233_USE_SUBMODULE -eq "1") {
    $Submodule = $true
}

$installFullPath = Join-Path (Get-Location).Path $InstallPath
$installParent = Split-Path $installFullPath -Parent
New-Item -ItemType Directory -Force -Path $installParent | Out-Null

if (Test-Path (Join-Path $installFullPath ".git")) {
    Write-Host "[hotc233-unity] Updating existing git checkout at $InstallPath"
    Invoke-Git -Arguments @("-C", $InstallPath, "pull", "--ff-only")
}
elseif (Test-Path $installFullPath) {
    throw "$InstallPath already exists but is not a git checkout. Move it away or delete it first."
}
elseif ($Submodule) {
    Write-Host "[hotc233-unity] Installing as git submodule at $InstallPath"
    Invoke-Git -Arguments @("submodule", "add", $RepoUrl, $InstallPath)
    Invoke-Git -Arguments @("submodule", "update", "--init", "--recursive", $InstallPath)
}
else {
    Write-Host "[hotc233-unity] Cloning to $InstallPath"
    Invoke-Git -Arguments @("clone", $RepoUrl, $InstallPath)
}

Write-Host "[hotc233-unity] Done. Open or refocus Unity and let AssetDatabase refresh."
