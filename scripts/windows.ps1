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
    [switch]$Force,
    [switch]$Network
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$OnWindows = $env:OS -eq 'Windows_NT'

function Find-Tool([string[]]$Names) {
    foreach ($Name in $Names) {
        $Command = Get-Command $Name -ErrorAction SilentlyContinue
        if ($null -ne $Command) { return $Command }
    }
    return $null
}

function Bash-Quote([string]$Value) {
    return "'" + $Value.Replace("'", "'\''") + "'"
}

function Wsl-Root {
    $Wsl = Find-Tool @('wsl.exe', 'wsl')
    if ($null -eq $Wsl) { return $null }
    try { & $Wsl.Source --status *> $null } catch { return $null }
    if ($LASTEXITCODE -ne 0) { return $null }
    $LinuxPath = (& $Wsl.Source wslpath -a -- $Root 2> $null | Select-Object -First 1)
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($LinuxPath)) { return $null }
    return $LinuxPath.Trim()
}

function Native-ToolchainReady {
    return $null -ne (Find-Tool @('make')) -and
           $null -ne (Find-Tool @('gcc')) -and
           $null -ne (Find-Tool @('ld')) -and
           $null -ne (Find-Tool @('objcopy')) -and
           $null -ne (Find-Tool @('python', 'py'))
}

function Select-Backend {
    if ($Native) {
        if (-not (Native-ToolchainReady)) {
            throw 'Native mode needs make, GCC, binutils and Python. Install the MSYS2 toolchain or omit -Native to use WSL2.'
        }
        return @{ Kind = 'native'; Root = $Root }
    }
    if ($OnWindows) {
        $LinuxRoot = Wsl-Root
        if ($null -ne $LinuxRoot) { return @{ Kind = 'wsl'; Root = $LinuxRoot } }
        if (Native-ToolchainReady) { return @{ Kind = 'native'; Root = $Root } }
        throw 'No usable build backend. Install WSL2 or an MSYS2 make/GCC/binutils/Python toolchain, then run .\start.ps1 doctor again.'
    }
    if (-not (Native-ToolchainReady)) { throw 'make, GCC, binutils and Python are required on this host.' }
    return @{ Kind = 'native'; Root = $Root }
}

function Invoke-NativeCommand([string]$Name, [string[]]$Arguments) {
    $Tool = Find-Tool @($Name)
    if ($null -eq $Tool) { throw "Missing command: $Name" }
    & $Tool.Source @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Name failed with exit code $LASTEXITCODE" }
}

function Invoke-BackendMake([hashtable]$Backend, [string[]]$Targets) {
    $ThreadCount = if ($Jobs -gt 0) { $Jobs } else { [Environment]::ProcessorCount }
    if ($Backend.Kind -eq 'native') {
        Push-Location $Root
        try { Invoke-NativeCommand 'make' (@("-j$ThreadCount") + $Targets) }
        finally { Pop-Location }
        return
    }
    $Wsl = Find-Tool @('wsl.exe', 'wsl')
    $TargetText = ($Targets | ForEach-Object { Bash-Quote $_ }) -join ' '
    $Command = "cd $(Bash-Quote $Backend.Root) && make -j$ThreadCount $TargetText"
    & $Wsl.Source bash -lc $Command
    if ($LASTEXITCODE -ne 0) { throw "WSL2 build failed with exit code $LASTEXITCODE" }
}

function Invoke-BackendStart([hashtable]$Backend, [string[]]$StartArguments) {
    if ($Backend.Kind -eq 'native') {
        $Python = Find-Tool @('python', 'py')
        if ($Python.Name -eq 'py.exe' -or $Python.Name -eq 'py') {
            & $Python.Source '-3' (Join-Path $Root 'start.py') @StartArguments
        } else {
            & $Python.Source (Join-Path $Root 'start.py') @StartArguments
        }
        if ($LASTEXITCODE -ne 0) { throw "Nuvora start failed with exit code $LASTEXITCODE" }
        return
    }
    $Wsl = Find-Tool @('wsl.exe', 'wsl')
    $ArgumentText = ($StartArguments | ForEach-Object { Bash-Quote $_ }) -join ' '
    $Command = "cd $(Bash-Quote $Backend.Root) && python3 start.py $ArgumentText"
    & $Wsl.Source bash -lc $Command
    if ($LASTEXITCODE -ne 0) { throw "Nuvora start failed with exit code $LASTEXITCODE" }
}

function Invoke-BackendPython([hashtable]$Backend, [string[]]$Arguments) {
    if ($Backend.Kind -eq 'native') {
        $Python = Find-Tool @('python', 'py')
        Push-Location $Root
        try {
            if ($Python.Name -eq 'py.exe' -or $Python.Name -eq 'py') {
                & $Python.Source '-3' @Arguments
            } else {
                & $Python.Source @Arguments
            }
            if ($LASTEXITCODE -ne 0) { throw "Python disk creation failed with exit code $LASTEXITCODE" }
        } finally {
            Pop-Location
        }
    } else {
        $Wsl = Find-Tool @('wsl.exe', 'wsl')
        $ArgumentText = ($Arguments | ForEach-Object { Bash-Quote $_ }) -join ' '
        & $Wsl.Source bash -lc "cd $(Bash-Quote $Backend.Root) && python3 $ArgumentText"
        if ($LASTEXITCODE -ne 0) { throw "Python disk creation failed with exit code $LASTEXITCODE" }
    }
}

function Show-Check([string]$Name, [bool]$Ok, [string]$Note) {
    $Mark = if ($Ok) { 'OK  ' } else { 'MISS' }
    $Color = if ($Ok) { 'Green' } else { 'Yellow' }
    Write-Host ("[{0}] {1,-20} {2}" -f $Mark, $Name, $Note) -ForegroundColor $Color
}

function Invoke-Doctor {
    Write-Host 'Nuvora Core host check' -ForegroundColor Cyan
    Write-Host "Source: $Root"
    Show-Check 'PowerShell' $true $PSVersionTable.PSVersion.ToString()
    Show-Check 'Python' ($null -ne (Find-Tool @('python', 'py'))) 'native/MSYS2 backend; WSL2 uses python3'
    Show-Check 'make' ($null -ne (Find-Tool @('make'))) 'native/MSYS2 backend'
    Show-Check 'GCC' ($null -ne (Find-Tool @('gcc'))) 'native/MSYS2 backend'
    Show-Check 'binutils' (($null -ne (Find-Tool @('ld'))) -and ($null -ne (Find-Tool @('objcopy'))) ) 'ld + objcopy'
    Show-Check 'QEMU x86_64' ($null -ne (Find-Tool @('qemu-system-x86_64'))) 'needed by run; WSL2 may provide it'
    Show-Check 'mtools' ($null -ne (Find-Tool @('mcopy'))) 'needed by UEFI ESP build'
    Show-Check 'xorriso' ($null -ne (Find-Tool @('xorriso'))) 'needed by ISO build'
    Show-Check 'qemu-img' ($null -ne (Find-Tool @('qemu-img'))) 'needed to make a VMware VMDK'
    if ($OnWindows) {
        $LinuxRoot = Wsl-Root
        Show-Check 'WSL2' ($null -ne $LinuxRoot) ($(if ($null -ne $LinuxRoot) { $LinuxRoot } else { 'install with: wsl --install' }))
    }
    $Backend = $null
    try { $Backend = Select-Backend } catch { Write-Host $_.Exception.Message -ForegroundColor Yellow }
    if ($null -ne $Backend) { Write-Host "Build backend: $($Backend.Kind)" -ForegroundColor Green }
    Write-Host 'Next: .\start.ps1 build; .\start.ps1 iso -Uefi; .\start.ps1 run -Uefi -Window' -ForegroundColor Cyan
}

try {
    switch ($Action) {
        'doctor' { Invoke-Doctor; break }
        'build' {
            $Backend = Select-Backend
            Invoke-BackendMake $Backend @('all')
            break
        }
        'iso' {
            $Backend = Select-Backend
            if ($Uefi) { Invoke-BackendMake $Backend @('esp', 'iso-uefi') }
            else { Invoke-BackendMake $Backend @('iso') }
            break
        }
        'run' {
            $Backend = Select-Backend
            $Arguments = @('--memory', "$Memory", '--disk-size', "$DiskSize", '--partitions', "$Partitions", '--disk-bus', $DiskBus)
            if ($Uefi) { $Arguments += '--uefi' }
            if ($Window) { $Arguments += '--window' }
            if ($Audio) { $Arguments += '--audio' }
            if ($NoUsb) { $Arguments += '--no-usb' }
            if ($NoEcam) { $Arguments += '--no-ecam' }
            if ($Network) { $Arguments += '--network' }
            # start.py creates a missing data image using the requested size and
            # partition count. make disk would create the default image first.
            $Targets = if ($Uefi) { @('all', 'esp') } else { @('all') }
            Invoke-BackendMake $Backend $Targets
            Invoke-BackendStart $Backend $Arguments
            break
        }
        'vmware' {
            $Backend = Select-Backend
            Invoke-BackendMake $Backend @('all', 'esp', 'iso-uefi')
            $DiskArgs = @('scripts/mkgptdisk.py', 'build/x86_64/nuvora-store.img',
                          '--if-missing', '--size', "$DiskSize", '--partitions', "$Partitions")
            Invoke-BackendPython $Backend $DiskArgs
            $Vmware = Join-Path $PSScriptRoot 'vmware.ps1'
            $VmwareArgs = @{ Name = $VmName; DiskController = $DiskController }
            if ($Force) { $VmwareArgs.Force = $true }
            & $Vmware @VmwareArgs
            break
        }
    }
} catch {
    Write-Error $_.Exception.Message
    exit 1
}
