$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Bin = Join-Path $Root "bin\Release"
$Dist = Join-Path $Root "dist"
$Stage = Join-Path $Dist "SSHLite-win-x64"

if (!(Test-Path (Join-Path $Bin "SSHLite.exe"))) {
    throw "SSHLite.exe not found. Build Release|x64 first."
}
if (!(Test-Path (Join-Path $Bin "SSHLiteAskPass.exe"))) {
    throw "SSHLiteAskPass.exe not found. Build Release|x64 first."
}

Remove-Item $Stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $Stage | Out-Null
Copy-Item (Join-Path $Bin "SSHLite.exe") $Stage
Copy-Item (Join-Path $Bin "SSHLiteAskPass.exe") $Stage
Copy-Item (Join-Path $Root "README.md") $Stage
Copy-Item (Join-Path $Root "LICENSE") $Stage

$Zip = Join-Path $Dist "SSHLite-win-x64.zip"
Remove-Item $Zip -Force -ErrorAction SilentlyContinue
Compress-Archive -Path "$Stage\*" -DestinationPath $Zip -CompressionLevel Optimal
Write-Host "Created $Zip"
