# Initialize MSVC + optional CMake configure for Tucano Engine.
$ErrorActionPreference = "Stop"

$VsPath = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
  -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
  -property installationPath

if (-not $VsPath) {
  throw "Visual Studio 2022 with C++ tools not found."
}

$VcVars = Join-Path $VsPath "VC\Auxiliary\Build\vcvars64.bat"
cmd /c "`"$VcVars`" && set" | ForEach-Object {
  if ($_ -match "^(.*?)=(.*)$") {
    [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2])
  }
}

if (-not $env:VCPKG_ROOT) {
  $env:VCPKG_ROOT = Join-Path $VsPath "VC\vcpkg"
}

Write-Host "MSVC ready: $(Get-Command cl.exe | Select-Object -ExpandProperty Source)"
Write-Host "DXC: $(Get-Command dxc.exe | Select-Object -ExpandProperty Source)"
Write-Host "VCPKG_ROOT=$env:VCPKG_ROOT"

if ($args -contains "configure") {
  Push-Location (Split-Path $PSScriptRoot -Parent)
  cmake --preset=windows-release
  Pop-Location
}

if ($args -contains "build") {
  Push-Location (Split-Path $PSScriptRoot -Parent)
  cmake --build --preset=windows-release
  Pop-Location
}
