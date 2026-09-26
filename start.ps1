[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('doctor', 'build', 'iso', 'run', 'vmware')]
    [string]$Action = 'doctor',
    [switch]$Native,
    [switch]$Uefi,
    [switch]$Window,
    [switch]$Audio,
    [switch]$NoUsb,
    [switch]$NoEcam,
    [switch]$Network,
    [ValidateSet('ide', 'ahci', 'nvme')]
    [string]$DiskBus = 'ide',
    [ValidateSet('ide', 'nvme')]
    [string]$DiskController = 'ide',
    [ValidateRange(32, 1048576)]
    [int]$Memory = 256,
    [ValidateRange(64, 137438953472)]
    [long]$DiskSize = 8192,
    [ValidateRange(1, 4)]
    [int]$Partitions = 1,
    [ValidateRange(0, 128)]
    [int]$Jobs = 0,
    [string]$VmName = 'NuvoraCore',
    [switch]$Force
)

$Script = Join-Path $PSScriptRoot 'scripts/windows.ps1'
& $Script @PSBoundParameters
if (-not $?) { exit 1 }
