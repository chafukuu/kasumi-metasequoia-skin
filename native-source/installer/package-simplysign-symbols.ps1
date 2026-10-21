# Build a full distributable installer with matching PDBs, then sign its uiAccess server and the
# finished installer with the real Certum certificate exposed by SimplySign Desktop.
#
# Typical usage:
#   pwsh -File .\package-simplysign-symbols.ps1 1.2.3

[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [Alias('TargetVersion')]
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version,

    [switch]$Reconfigure,
    [string]$CertificateThumbprint,

    [ValidateNotNullOrEmpty()]
    [string]$TimestampUrl = 'http://time.certum.pl',

    [string]$IsccPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$packageArguments = @{
    Version = $Version
    IncludeSymbols = $true
    Reconfigure = $Reconfigure
    TimestampUrl = $TimestampUrl
}
if ($CertificateThumbprint) {
    $packageArguments.CertificateThumbprint = $CertificateThumbprint
}
if ($IsccPath) {
    $packageArguments.IsccPath = $IsccPath
}

& (Join-Path $PSScriptRoot 'package-simplysign.ps1') @packageArguments
