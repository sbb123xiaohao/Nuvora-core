[CmdletBinding()]
param(
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$')]
    [string]$Name = 'NuvoraCore',
    [ValidateSet('ide', 'nvme')]
    [string]$DiskController = 'ide',
    [string]$DiskImage,
    [string]$IsoImage,
    [string]$OutputDirectory,
    [ValidateRange(256, 1048576)]
    [int]$Memory = 2048,
    [ValidateRange(1, 1)]
    [int]$Cpus = 1,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) { $OutputDirectory = Join-Path $Root 'vmware' }
if ([string]::IsNullOrWhiteSpace($DiskImage)) { $DiskImage = Join-Path $Root 'build\x86_64\nuvora-store.img' }

function Find-Tool([string]$Name) { Get-Command $Name -ErrorAction SilentlyContinue }
function Rel([string]$Path) { return [IO.Path]::GetFileName($Path) }

$DiskImage = [IO.Path]::GetFullPath($DiskImage)
if (-not (Test-Path -LiteralPath $DiskImage -PathType Leaf)) {
    throw "Missing data image: $DiskImage. Run .\start.ps1 vmware from the project root to build it, or pass -DiskImage."
}

if ([string]::IsNullOrWhiteSpace($IsoImage)) {
    $Candidates = Get-ChildItem (Join-Path $Root 'build\x86_64') -Filter '*-uefi.iso' -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending
    if ($Candidates) { $IsoImage = $Candidates[0].FullName }
}
if ([string]::IsNullOrWhiteSpace($IsoImage) -or -not (Test-Path -LiteralPath $IsoImage -PathType Leaf)) {
    throw 'A UEFI ISO is required for a bootable VMware profile. Run .\start.ps1 iso -Uefi first, or pass -IsoImage <path>.'
}
$IsoImage = [IO.Path]::GetFullPath($IsoImage)
if ($IsoImage.Contains('"')) { throw 'The ISO path cannot contain a double quote in a VMware configuration.' }

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$VmDirectory = Join-Path $OutputDirectory $Name
New-Item -ItemType Directory -Force -Path $VmDirectory | Out-Null
$Vmdk = Join-Path $VmDirectory "$Name.vmdk"
$Vmx = Join-Path $VmDirectory "$Name.vmx"
if ((Test-Path -LiteralPath $Vmx) -and -not $Force) {
    throw "Profile already exists: $Vmx. Use -Force to regenerate it."
}
$QemuImg = Find-Tool 'qemu-img'
if (-not (Test-Path -LiteralPath $Vmdk)) {
    $TemporaryVmdk = Join-Path $VmDirectory ("$Name." + [guid]::NewGuid().ToString('N') + '.partial.vmdk')
    try {
        if ($null -ne $QemuImg) {
            & $QemuImg.Source convert -p -f raw -O vmdk -o subformat=monolithicSparse $DiskImage $TemporaryVmdk
        } else {
            $Wsl = Find-Tool 'wsl.exe'
            if ($null -eq $Wsl) {
                throw 'qemu-img is required to convert the sparse raw image to VMDK. Install QEMU for Windows or qemu-utils in WSL2.'
            }
            $LinuxDisk = (& $Wsl.Source wslpath -a -- $DiskImage | Select-Object -First 1).Trim()
            $LinuxVmdk = (& $Wsl.Source wslpath -a -- $TemporaryVmdk | Select-Object -First 1).Trim()
            & $Wsl.Source --exec qemu-img convert -p -f raw -O vmdk -o subformat=monolithicSparse $LinuxDisk $LinuxVmdk
        }
        if ($LASTEXITCODE -ne 0) { throw "qemu-img failed with exit code $LASTEXITCODE" }
        Move-Item -LiteralPath $TemporaryVmdk -Destination $Vmdk
    } finally {
        if (Test-Path -LiteralPath $TemporaryVmdk) { Remove-Item -LiteralPath $TemporaryVmdk }
    }
}

$DiskLines = if ($DiskController -eq 'nvme') {
    @('nvme0.present = "TRUE"', 'nvme0:0.present = "TRUE"', "nvme0:0.fileName = `"$(Rel $Vmdk)`"", 'nvme0:0.deviceType = "disk"')
} else {
    @('ide0:0.present = "TRUE"', "ide0:0.fileName = `"$(Rel $Vmdk)`"", 'ide0:0.deviceType = "disk"')
}
$Lines = @(
    '.encoding = "UTF-8"',
    'config.version = "8"',
    'virtualHW.version = "19"',
    "displayName = `"$Name`"",
    'guestOS = "other-64"',
    'firmware = "efi"',
    'uefi.secureBoot.enabled = "FALSE"',
    "nvram = `"$Name.nvram`"",
    "memsize = `"$Memory`"",
    "numvcpus = `"$Cpus`"",
    "cpuid.coresPerSocket = `"$Cpus`"",
    'ethernet0.present = "TRUE"',
    'ethernet0.connectionType = "nat"',
    'ethernet0.virtualDev = "e1000e"',
    'ethernet0.wakeOnPcktRcv = "FALSE"',
    'usb.present = "TRUE"',
    'usb_xhci.present = "TRUE"',
    'sound.present = "TRUE"',
    'sound.virtualDev = "hdaudio"',
    'svga.present = "TRUE"',
    'mks.enable3d = "FALSE"',
    'ide1:0.present = "TRUE"',
    'ide1:0.deviceType = "cdrom-image"',
    "ide1:0.fileName = `"$IsoImage`"",
    'ide1:0.startConnected = "TRUE"'
) + $DiskLines
[IO.File]::WriteAllLines($Vmx, $Lines, [Text.UTF8Encoding]::new($false))

Write-Host "VMware profile: $Vmx" -ForegroundColor Green
Write-Host "Disk controller: $DiskController; network: e1000e; firmware: UEFI; Secure Boot: off" -ForegroundColor Cyan
Write-Host 'Open the .vmx in VMware Workstation 17, keep the USB 3.x controller, and choose NAT or Bridged in the adapter settings.'
