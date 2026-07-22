# Fetch / prepare 3thirdy SDKs
# Aftermath binary SDK requires NVIDIA login — this script only clones free samples
# and prints drop-in instructions.

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root

$samples = Join-Path $Root "nsight-aftermath-samples"
if (-not (Test-Path (Join-Path $samples ".git"))) {
  Write-Host "Cloning NVIDIA/nsight-aftermath-samples ..."
  git clone --depth 1 https://github.com/NVIDIA/nsight-aftermath-samples.git $samples
} else {
  Write-Host "nsight-aftermath-samples already present."
}

$afInc = Join-Path $Root "nsight-aftermath\include\GFSDK_Aftermath.h"
if (Test-Path $afInc) {
  Write-Host "Aftermath SDK: FOUND at 3thirdy/nsight-aftermath"
} else {
  Write-Host @"

Aftermath SDK: NOT FOUND
  1) Download Windows SDK from:
     https://developer.nvidia.com/nsight-aftermath/getting-started
  2) Extract so this file exists:
     3thirdy/nsight-aftermath/include/GFSDK_Aftermath.h
  3) Re-run cmake --preset=windows-release

"@
}

Write-Host "Done."
