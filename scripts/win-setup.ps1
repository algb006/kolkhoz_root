#Requires -Version 5.1
<#
.SYNOPSIS
    Prepares this Windows host as the MSVC build machine for the simulation core.

.DESCRIPTION
    Run once, from an elevated PowerShell. The script is idempotent: every step
    checks the current state first, so re-running it after a change is safe.

    What it sets up:
      * OpenSSH Server: service on automatic start, key-only login for the
        build account, firewall opened for the build VM alone.
      * MSYS2: provides rsync and a POSIX shell. rsync over SSH needs a shell
        that understands /c/... paths and forwards arguments unmangled, which
        cmd.exe does not; sshd is therefore pointed at MSYS2 bash.
      * Visual Studio Build Tools with the C++ workload, CMake and Ninja.
      * The project directory under the game's working folder.

.PARAMETER PublicKey
    The build VM's public key: either the key text itself or a path to a
    .pub file. Without it the key step is skipped and everything else runs.

.PARAMETER ClientAddress
    Address of the build VM. The firewall rule allows port 22 from it only.

.PARAMETER Root
    Where the core sources are rsynced to on this host.

.PARAMETER SkipInstall
    Do not install MSYS2 or Build Tools, only configure what is present.

.PARAMETER LockPasswordAuth
    Turn password authentication off. Run this only after a key login has
    been verified, otherwise the account becomes unreachable over SSH.

.EXAMPLE
    .\win-setup.ps1 -PublicKey "ssh-ed25519 AAAA... core-vm@alex-linux"
#>

param(
    [string]$PublicKey     = '',
    [string]$ClientAddress = '192.168.1.6',
    [string]$Root          = 'C:\MyGames\Kolkhoz\core-msvc',
    [string]$MsysRoot      = 'C:\msys64',
    [switch]$SkipInstall,
    [switch]$LockPasswordAuth
)

$ErrorActionPreference = 'Stop'

function Step($text) { Write-Host "`n==> $text" -ForegroundColor Cyan }
function Ok  ($text) { Write-Host "    $text"   -ForegroundColor Green }
function Warn($text) { Write-Host "    $text"   -ForegroundColor Yellow }

$identity  = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Нужен PowerShell от администратора.'
}

# Built-in group names are localised; SIDs are not.
$sidAdmins = 'S-1-5-32-544'
$sidSystem = 'S-1-5-18'

# ---------------------------------------------------------------------------
# 1. OpenSSH Server
# ---------------------------------------------------------------------------

Step 'OpenSSH Server'

$capability = Get-WindowsCapability -Online |
              Where-Object { $_.Name -like 'OpenSSH.Server*' -and $_.State -ne 'Installed' }
if ($capability) {
    Add-WindowsCapability -Online -Name $capability.Name | Out-Null
    Ok 'компонент доустановлен'
}

Set-Service -Name sshd -StartupType Automatic
if ((Get-Service sshd).Status -ne 'Running') { Start-Service sshd }
Ok ('служба sshd: ' + (Get-Service sshd).Status + ', автозапуск')

# ssh-agent is not needed for inbound sessions; left alone on purpose.

# ---------------------------------------------------------------------------
# 2. MSYS2 — rsync and a POSIX shell for sshd
# ---------------------------------------------------------------------------

Step 'MSYS2'

$bash = Join-Path $MsysRoot 'usr\bin\bash.exe'
if (-not (Test-Path $bash)) {
    if ($SkipInstall) { throw "MSYS2 не найден в $MsysRoot, а установка отключена." }
    Ok 'ставлю MSYS2 через winget'
    winget install --id MSYS2.MSYS2 -e --accept-package-agreements --accept-source-agreements
    if (-not (Test-Path $bash)) { throw "MSYS2 не встал в $MsysRoot." }
}

# -l loads /etc/profile, which is what puts /usr/bin on PATH.
& $bash -lc 'pacman -Syu --noconfirm --disable-download-timeout' | Out-Null
& $bash -lc 'pacman -S --needed --noconfirm rsync'                | Out-Null

# A fresh MSYS2 sometimes shuts the shell down mid-update; the second run
# finishes the job. Cheap to repeat, and the alternative is a silent failure
# that only shows up as "rsync: command not found" during the first sync.
$rsyncVersion = (& $bash -lc 'rsync --version 2>/dev/null | head -1')
if (-not $rsyncVersion) {
    & $bash -lc 'pacman -S --needed --noconfirm rsync' | Out-Null
    $rsyncVersion = (& $bash -lc 'rsync --version 2>/dev/null | head -1')
}
if (-not $rsyncVersion) { throw 'rsync в MSYS2 не встал — повтори pacman -S rsync вручную.' }
Ok $rsyncVersion

# ---------------------------------------------------------------------------
# 3. sshd default shell
# ---------------------------------------------------------------------------
#
# rsync runs `rsync --server ...` on the far side and expects POSIX argument
# handling. With cmd.exe as the default shell the command line is mangled and
# the transfer fails with a "protocol version mismatch" that has nothing to do
# with versions. Escaping is switched off for the same reason.

Step 'Оболочка по умолчанию для sshd'

if (-not (Test-Path 'HKLM:\SOFTWARE\OpenSSH')) {
    New-Item -Path 'HKLM:\SOFTWARE\OpenSSH' -Force | Out-Null
}
New-ItemProperty -Path 'HKLM:\SOFTWARE\OpenSSH' -Name DefaultShell `
                 -Value $bash -PropertyType String -Force | Out-Null
New-ItemProperty -Path 'HKLM:\SOFTWARE\OpenSSH' -Name DefaultShellCommandOption `
                 -Value '-lc' -PropertyType String -Force | Out-Null
New-ItemProperty -Path 'HKLM:\SOFTWARE\OpenSSH' -Name DefaultShellEscapeArguments `
                 -Value 0 -PropertyType DWord -Force | Out-Null
Ok "$bash -lc"

# ---------------------------------------------------------------------------
# 4. Key-based login
# ---------------------------------------------------------------------------
#
# For an account in the administrators group sshd ignores ~\.ssh\authorized_keys
# and reads the shared file below — and only if its ACL grants nobody but
# SYSTEM and the administrators group. A single extra entry there and the key
# is silently refused.

Step 'Вход по ключу'

if ($PublicKey) {
    $keyText = if (Test-Path -LiteralPath $PublicKey) {
        (Get-Content -LiteralPath $PublicKey -Raw).Trim()
    } else { $PublicKey.Trim() }

    if ($keyText -notmatch '^(ssh-ed25519|ssh-rsa|ecdsa-sha2-\S+)\s+\S+') {
        throw 'Это не похоже на публичный ключ.'
    }

    $adminKeys = 'C:\ProgramData\ssh\administrators_authorized_keys'
    $existing  = if (Test-Path $adminKeys) { Get-Content $adminKeys } else { @() }

    # Compare by the key body: the trailing comment changes freely.
    $body = ($keyText -split '\s+')[1]
    if ($existing -match [regex]::Escape($body)) {
        Ok 'ключ уже прописан'
    } else {
        ($existing + $keyText) | Where-Object { $_ -ne '' } |
            Set-Content -Path $adminKeys -Encoding ascii
        Ok 'ключ добавлен'
    }

    icacls.exe $adminKeys /inheritance:r /grant "*${sidSystem}:F" "*${sidAdmins}:F" | Out-Null
    Ok 'права на файл ключей выставлены'
} else {
    Warn 'ключ не передан — параметр -PublicKey пропущен'
}

# ---------------------------------------------------------------------------
# 5. sshd_config
# ---------------------------------------------------------------------------

Step 'sshd_config'

$config = 'C:\ProgramData\ssh\sshd_config'
$backup = "$config.bak"
if (-not (Test-Path $backup)) { Copy-Item $config $backup }

$text = Get-Content $config -Raw
function Set-Directive([string]$body, [string]$name, [string]$value) {
    # Only the first occurrence is rewritten, which is the commented default in
    # the global part. The Match block at the end of the file stays untouched:
    # it is what redirects administrators to administrators_authorized_keys.
    $rx = [regex]::new('(?m)^[ \t]*#?[ \t]*' + [regex]::Escape($name) + '[ \t]+.*$')
    if ($rx.IsMatch($body)) { return $rx.Replace($body, "$name $value", 1) }
    return $body.TrimEnd() + "`r`n$name $value`r`n"
}

$text = Set-Directive $text 'PubkeyAuthentication' 'yes'
if ($LockPasswordAuth) { $text = Set-Directive $text 'PasswordAuthentication' 'no' }

Set-Content -Path $config -Value $text -Encoding ascii
Ok ('PubkeyAuthentication yes' + $(if ($LockPasswordAuth) { ', PasswordAuthentication no' } else { '' }))
if (-not $LockPasswordAuth) {
    Warn 'пароль пока разрешён; выключить — перезапуск с -LockPasswordAuth, но сначала проверь вход по ключу'
}

Restart-Service sshd
Ok 'sshd перезапущен'

# ---------------------------------------------------------------------------
# 6. Network and firewall
# ---------------------------------------------------------------------------
#
# A "Public" profile blocks everything inbound, ping included — which is what
# an unreachable host with a resolving ARP entry looks like from the VM.

Step 'Сеть и брандмауэр'

Get-NetConnectionProfile | ForEach-Object {
    if ($_.NetworkCategory -eq 'Public') {
        Set-NetConnectionProfile -InterfaceIndex $_.InterfaceIndex -NetworkCategory Private
        Ok ("профиль сети '$($_.Name)': Public -> Private")
    } else {
        Ok ("профиль сети '$($_.Name)': $($_.NetworkCategory)")
    }
}

# The rule shipped with the OpenSSH feature is left as it is; ours is narrower
# and named, so it is obvious later what opened the port and for whom.
$ruleName = 'Kolkhoz-core-ssh'
Get-NetFirewallRule -Name $ruleName -ErrorAction SilentlyContinue | Remove-NetFirewallRule
New-NetFirewallRule -Name $ruleName -DisplayName 'Ядро колхоза: SSH со сборочной виртуалки' `
    -Direction Inbound -Action Allow -Protocol TCP -LocalPort 22 `
    -RemoteAddress $ClientAddress -Profile Any -Enabled True | Out-Null
Ok "порт 22 открыт для $ClientAddress"

$pingRule = 'Kolkhoz-core-ping'
Get-NetFirewallRule -Name $pingRule -ErrorAction SilentlyContinue | Remove-NetFirewallRule
New-NetFirewallRule -Name $pingRule -DisplayName 'Ядро колхоза: ping со сборочной виртуалки' `
    -Direction Inbound -Action Allow -Protocol ICMPv4 -IcmpType 8 `
    -RemoteAddress $ClientAddress -Profile Any -Enabled True | Out-Null
Ok "ping разрешён для $ClientAddress"

# ---------------------------------------------------------------------------
# 7. Visual Studio Build Tools
# ---------------------------------------------------------------------------

Step 'MSVC'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath  = if (Test-Path $vswhere) {
    & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
               -property installationPath
} else { $null }

if (-not $vsPath) {
    if ($SkipInstall) { throw 'MSVC не найден, а установка отключена.' }
    Ok 'ставлю Build Tools через winget — это надолго'
    $components = @(
        '--add Microsoft.VisualStudio.Workload.VCTools'
        '--add Microsoft.VisualStudio.Component.VC.Tools.x86.x64'
        '--add Microsoft.VisualStudio.Component.Windows11SDK.22621'
        '--add Microsoft.VisualStudio.Component.VC.CMake.Project'
    ) -join ' '
    winget install --id Microsoft.VisualStudio.2022.BuildTools -e `
        --accept-package-agreements --accept-source-agreements `
        --override "--quiet --wait --norestart $components"
    $vsPath = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $vsPath) { throw 'Build Tools не встали.' }
Ok $vsPath

# ---------------------------------------------------------------------------
# 8. Project directory
# ---------------------------------------------------------------------------

Step 'Каталог проекта'

New-Item -ItemType Directory -Path $Root -Force | Out-Null
Ok $Root

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

$msysRootPosix = '/' + ($Root -replace '^([A-Za-z]):', '$1' -replace '\\', '/')
$msysRootPosix = $msysRootPosix.Substring(0,2).ToLower() + $msysRootPosix.Substring(2)

Write-Host "`n--- на виртуалке ---" -ForegroundColor Cyan
Write-Host @"
~/.ssh/config:

  Host win
      HostName $((Get-NetIPAddress -AddressFamily IPv4 |
                   Where-Object { $_.IPAddress -notlike '127.*' -and $_.IPAddress -notlike '169.254.*' } |
                   Select-Object -First 1 -ExpandProperty IPAddress))
      User $env:USERNAME
      IdentityFile ~/.ssh/id_ed25519_win
      IdentitiesOnly yes

Проверка:  ssh win 'uname -a; rsync --version | head -1'
Сборка:    make win WIN_DIR=$msysRootPosix
"@
