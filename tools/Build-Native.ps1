[CmdletBinding()]
param([string]$BuildDirectory = '', [ValidateRange(1,32)][int]$Parallel = 4)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if (-not $BuildDirectory) { $BuildDirectory=Join-Path $root 'build\native' }
if (-not $env:VCPKG_ROOT -or -not (Test-Path -LiteralPath (Join-Path $env:VCPKG_ROOT 'scripts\buildsystems\vcpkg.cmake'))) { throw 'Set VCPKG_ROOT to an existing vcpkg checkout.' }
if (-not $env:BOOST_ROOT -or -not (Test-Path -LiteralPath $env:BOOST_ROOT)) { throw 'Set BOOST_ROOT to the installed x64-windows-static-md Boost directory.' }
[void](Get-Command cl.exe -ErrorAction Stop)
[void](Get-Command ninja.exe -ErrorAction Stop)
[Environment]::SetEnvironmentVariable('CL',$null,'Process')
& cmake -S (Join-Path $root 'native-source\server') -B $BuildDirectory -G 'Ninja Multi-Config' '-DCMAKE_CONFIGURATION_TYPES=Release' `
    "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" '-DVCPKG_TARGET_TRIPLET=x64-windows' `
    "-DBoost_ROOT=$env:BOOST_ROOT" '-DMSIME_PRODUCT_VERSION_OVERRIDE=0.11.1'
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
& cmake --build $BuildDirectory --config Release --target MetasequoiaImeServer --parallel $Parallel
if ($LASTEXITCODE -ne 0) { throw 'Native build failed.' }
