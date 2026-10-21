# Build a light (hotfix) installer that only replaces TSF, Server and the settings window assets, then sign its payload
# and the finished installer with the real Certum certificate exposed by SimplySign Desktop.
#
# The light installer carries no dictionaries, neural models or factory config template, so it is
# only valid on top of an existing full install. Use it for hotfixes that do not add config keys or
# change data files; otherwise publish a full installer with package-simplysign.ps1.
#
# Typical usage:
#   pwsh -File .\package-simplysign-light.ps1 1.2.4
#   pwsh -File .\package-simplysign-light.ps1 1.2.4 -IncludeSymbols

[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [Alias('TargetVersion')]
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version,

    [switch]$IncludeSymbols,
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
    Light = $true
    IncludeSymbols = $IncludeSymbols
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
