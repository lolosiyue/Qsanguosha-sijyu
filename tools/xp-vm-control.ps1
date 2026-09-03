[CmdletBinding()]
param(
    [ValidateSet(
        'Status',
        'Start',
        'WaitGuestControl',
        'RunGuest',
        'RunInteractive',
        'CopyToGuest',
        'CopyFromGuest',
        'Stop',
        'RestoreSnapshot'
    )]
    [string]$Action = 'Status',
    [string]$VmName = 'QSanguosha-XP-SP3-x86-PoC',
    [string]$VBoxManage = 'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe',
    [string]$CredentialFile = '',
    [ValidateSet('gui', 'headless')]
    [string]$StartType = 'gui',
    [string]$GuestCommand = '',
    [string[]]$GuestArguments = @(),
    [string]$GuestCommandLine = '',
    [string]$ExpectedProcess = '',
    [string]$HostPath = '',
    [string]$GuestPath = '',
    [string]$SnapshotName = '',
    [ValidateRange(5, 600)]
    [int]$TimeoutSeconds = 90,
    [switch]$ForcePowerOff
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($CredentialFile)) {
    $CredentialFile = Join-Path $repoRoot 'builds\xp-poc\CodexQA.credential.xml'
}

if (-not (Test-Path -LiteralPath $VBoxManage -PathType Leaf)) {
    throw "VBoxManage not found: $VBoxManage"
}

$script:GuestCredential = $null

function Invoke-VBox([string[]]$Arguments, [switch]$AllowFailure) {
    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $lines = @(& $VBoxManage @Arguments 2>&1 | ForEach-Object { $_.ToString() })
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    $output = $lines -join [Environment]::NewLine
    if ($exitCode -ne 0 -and -not $AllowFailure) {
        throw "VBoxManage failed ($exitCode): $output"
    }
    return [pscustomobject]@{
        ExitCode = $exitCode
        Output = $output
    }
}

function Get-GuestCredential {
    if ($null -ne $script:GuestCredential) {
        return $script:GuestCredential
    }
    if (-not (Test-Path -LiteralPath $CredentialFile -PathType Leaf)) {
        throw "Guest credential file not found: $CredentialFile"
    }

    $credential = Import-Clixml -LiteralPath $CredentialFile
    if ($credential -isnot [System.Management.Automation.PSCredential]) {
        throw "Credential file does not contain a PSCredential: $CredentialFile"
    }
    $script:GuestCredential = $credential
    return $credential
}

function Invoke-GuestControl(
    [string]$Subcommand,
    [string[]]$Arguments,
    [switch]$AllowFailure
) {
    $credential = Get-GuestCredential
    $passwordFile = Join-Path ([IO.Path]::GetTempPath()) ("qsan-vbox-{0}.txt" -f [Guid]::NewGuid())
    $utf8WithoutBom = [System.Text.UTF8Encoding]::new($false)
    try {
        # Keep the password out of the VBoxManage command line. The temporary
        # plaintext file exists only for the duration of this invocation.
        [IO.File]::WriteAllText(
            $passwordFile,
            $credential.GetNetworkCredential().Password,
            $utf8WithoutBom
        )
        $vboxArguments = @(
            'guestcontrol',
            $VmName,
            $Subcommand,
            "--username=$($credential.UserName)",
            "--passwordfile=$passwordFile"
        ) + $Arguments
        return Invoke-VBox -Arguments $vboxArguments -AllowFailure:$AllowFailure
    } finally {
        if (Test-Path -LiteralPath $passwordFile -PathType Leaf) {
            Remove-Item -LiteralPath $passwordFile -Force
        }
    }
}

function Invoke-GuestProgram(
    [string]$Program,
    [string[]]$ProgramArguments = @(),
    [int]$CommandTimeoutSeconds = $TimeoutSeconds,
    [switch]$NoWait,
    [switch]$AllowFailure
) {
    $arguments = @(
        "--exe=$Program",
        "--timeout=$($CommandTimeoutSeconds * 1000)"
    )
    if ($NoWait) {
        $arguments += @('--no-wait-stdout', '--no-wait-stderr')
    } else {
        $arguments += @('--wait-stdout', '--wait-stderr')
    }
    $arguments += '--'
    $arguments += $Program
    if ($ProgramArguments.Count -gt 0) {
        $arguments += $ProgramArguments
    }
    return Invoke-GuestControl -Subcommand 'run' -Arguments $arguments -AllowFailure:$AllowFailure
}

function Get-VmInfo {
    $result = Invoke-VBox -Arguments @('showvminfo', $VmName, '--machinereadable')
    $values = @{}
    foreach ($line in ($result.Output -split "`r?`n")) {
        if ($line -match '^([^=]+)="(.*)"$') {
            $values[$Matches[1]] = $Matches[2]
        }
    }
    return $values
}

function Get-VmState {
    $info = Get-VmInfo
    if (-not $info.ContainsKey('VMState')) {
        throw "VMState is missing for VM: $VmName"
    }
    return $info['VMState']
}

function Assert-VmRunning {
    $state = Get-VmState
    if ($state -ne 'running') {
        throw "VM '$VmName' is not running (state=$state). Use -Action Start first."
    }
}

function Test-GuestControlReady {
    $result = Invoke-GuestProgram `
        -Program 'C:\WINDOWS\system32\cmd.exe' `
        -ProgramArguments @('/c', 'echo GUEST_CONTROL_READY') `
        -CommandTimeoutSeconds 10 `
        -AllowFailure
    return $result.ExitCode -eq 0 -and $result.Output -match 'GUEST_CONTROL_READY'
}

function Wait-GuestControlReady {
    Assert-VmRunning
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        if (Test-GuestControlReady) {
            Write-Output "Guest Control ready: $VmName"
            return
        }
        Start-Sleep -Seconds 2
    } while ([DateTime]::UtcNow -lt $deadline)

    throw "Guest Control did not become ready within $TimeoutSeconds seconds."
}

function Get-GuestNextMinute {
    $result = Invoke-GuestProgram `
        -Program 'C:\WINDOWS\system32\cmd.exe' `
        -ProgramArguments @('/c', 'echo %TIME%') `
        -CommandTimeoutSeconds 10
    if ($result.Output -notmatch '(?m)^\s*(\d{1,2}):(\d{2}):') {
        throw "Cannot parse guest time: $($result.Output)"
    }

    $guestTime = [DateTime]::new(2000, 1, 1, [int]$Matches[1], [int]$Matches[2], 0)
    return $guestTime.AddMinutes(1).ToString('HH:mm', [Globalization.CultureInfo]::InvariantCulture)
}

function Get-GuestProcessIds([string]$ProcessName) {
    $result = Invoke-GuestProgram `
        -Program 'C:\WINDOWS\system32\tasklist.exe' `
        -ProgramArguments @('/FI', "IMAGENAME eq $ProcessName", '/FO', 'CSV', '/NH') `
        -CommandTimeoutSeconds 10 `
        -AllowFailure
    $processIds = @()
    if ($result.ExitCode -eq 0) {
        foreach ($line in ($result.Output -split "`r?`n")) {
            if ($line -match '^"[^"]+","(\d+)"') {
                $processIds += [int]$Matches[1]
            }
        }
    }
    return $processIds
}

function Wait-NewGuestProcess([string]$ProcessName, [int[]]$ExistingProcessIds) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        foreach ($processId in (Get-GuestProcessIds -ProcessName $ProcessName)) {
            if ($ExistingProcessIds -notcontains $processId) {
                Write-Output "New guest process detected: $ProcessName (PID $processId)"
                return
            }
        }
        Start-Sleep -Seconds 2
    } while ([DateTime]::UtcNow -lt $deadline)

    throw "A new '$ProcessName' process was not detected within $TimeoutSeconds seconds."
}

switch ($Action) {
    'Status' {
        $info = Get-VmInfo
        $guestReady = $false
        $explorerLine = ''
        if ($info['VMState'] -eq 'running' -and
            (Test-Path -LiteralPath $CredentialFile -PathType Leaf)) {
            $guestReady = Test-GuestControlReady
            if ($guestReady) {
                $taskList = Invoke-GuestProgram `
                    -Program 'C:\WINDOWS\system32\tasklist.exe' `
                    -ProgramArguments @('/V', '/FI', 'IMAGENAME eq explorer.exe', '/NH') `
                    -CommandTimeoutSeconds 10 `
                    -AllowFailure
                $explorerLine = (($taskList.Output -split "`r?`n") |
                    Where-Object { $_ -match '(?i)explorer\.exe' }) -join ' '
            }
        }
        [pscustomobject]@{
            VM = $VmName
            State = $info['VMState']
            Nic1 = $info['nic1']
            GuestControlReady = $guestReady
            Explorer = $explorerLine
            CredentialFile = $CredentialFile
        }
    }

    'Start' {
        $state = Get-VmState
        if ($state -ne 'running') {
            $result = Invoke-VBox -Arguments @('startvm', $VmName, '--type', $StartType)
            Write-Output $result.Output
        } else {
            Write-Output "VM already running: $VmName"
        }
        Wait-GuestControlReady
    }

    'WaitGuestControl' {
        Wait-GuestControlReady
    }

    'RunGuest' {
        Assert-VmRunning
        if ([string]::IsNullOrWhiteSpace($GuestCommand)) {
            throw '-GuestCommand is required for RunGuest.'
        }
        $result = Invoke-GuestProgram -Program $GuestCommand -ProgramArguments $GuestArguments
        Write-Output $result.Output
    }

    'RunInteractive' {
        Assert-VmRunning
        if ([string]::IsNullOrWhiteSpace($GuestCommandLine)) {
            throw '-GuestCommandLine is required for RunInteractive.'
        }
        if ($GuestCommandLine -match '[\r\n"]') {
            throw 'GuestCommandLine must be a single command without double quotes. Prefer an absolute .cmd path without spaces.'
        }

        $existingProcessIds = @()
        if (-not [string]::IsNullOrWhiteSpace($ExpectedProcess)) {
            $existingProcessIds = @(Get-GuestProcessIds -ProcessName $ExpectedProcess)
        }

        # Guest Control cannot attach a GUI process to the logged-in XP desktop.
        # XP AT /interactive schedules it in the visible session on the next minute.
        $scheduledTime = Get-GuestNextMinute
        $result = Invoke-GuestProgram `
            -Program 'C:\WINDOWS\system32\at.exe' `
            -ProgramArguments @($scheduledTime, '/interactive', $GuestCommandLine) `
            -CommandTimeoutSeconds 10
        Write-Output "Interactive command scheduled for guest time $scheduledTime (allow up to 60 seconds)."
        Write-Output $result.Output
        if (-not [string]::IsNullOrWhiteSpace($ExpectedProcess)) {
            Wait-NewGuestProcess `
                -ProcessName $ExpectedProcess `
                -ExistingProcessIds $existingProcessIds
        }
    }

    'CopyToGuest' {
        Assert-VmRunning
        if ([string]::IsNullOrWhiteSpace($HostPath) -or
            -not (Test-Path -LiteralPath $HostPath)) {
            throw "HostPath does not exist: $HostPath"
        }
        if ([string]::IsNullOrWhiteSpace($GuestPath)) {
            throw '-GuestPath is required for CopyToGuest.'
        }
        $resolvedHostPath = (Resolve-Path -LiteralPath $HostPath).Path
        $result = Invoke-GuestControl -Subcommand 'copyto' -Arguments @($resolvedHostPath, $GuestPath)
        Write-Output $result.Output
    }

    'CopyFromGuest' {
        Assert-VmRunning
        if ([string]::IsNullOrWhiteSpace($GuestPath)) {
            throw '-GuestPath is required for CopyFromGuest.'
        }
        if ([string]::IsNullOrWhiteSpace($HostPath) -or
            -not (Test-Path -LiteralPath $HostPath -PathType Container)) {
            throw 'HostPath must be an existing destination directory for CopyFromGuest.'
        }
        $resolvedHostDirectory = (Resolve-Path -LiteralPath $HostPath).Path
        $result = Invoke-GuestControl `
            -Subcommand 'copyfrom' `
            -Arguments @('--recursive', "--target-directory=$resolvedHostDirectory", $GuestPath)
        Write-Output $result.Output
    }

    'Stop' {
        $state = Get-VmState
        if ($state -ne 'running') {
            Write-Output "VM is not running: $VmName (state=$state)"
            break
        }

        $shutdownExitCode = -1
        try {
            $shutdown = Invoke-GuestProgram `
                -Program 'C:\WINDOWS\system32\shutdown.exe' `
                -ProgramArguments @('-s', '-f', '-t', '0') `
                -CommandTimeoutSeconds 10 `
                -NoWait `
                -AllowFailure
            $shutdownExitCode = $shutdown.ExitCode
        } catch {
            if (-not $ForcePowerOff) {
                throw
            }
            Write-Warning "Guest shutdown could not start: $($_.Exception.Message)"
        }
        $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        do {
            Start-Sleep -Seconds 2
            $state = Get-VmState
            if ($state -eq 'poweroff') {
                Write-Output "VM powered off cleanly: $VmName"
                break
            }
        } while ([DateTime]::UtcNow -lt $deadline)

        if ($state -ne 'poweroff') {
            if (-not $ForcePowerOff) {
                throw "Guest shutdown failed or timed out (guestcontrol exit=$shutdownExitCode). Re-run with -ForcePowerOff only if discarding guest state is acceptable."
            }
            $result = Invoke-VBox -Arguments @('controlvm', $VmName, 'poweroff')
            Write-Output $result.Output
        }
    }

    'RestoreSnapshot' {
        if ([string]::IsNullOrWhiteSpace($SnapshotName)) {
            throw '-SnapshotName is required for RestoreSnapshot.'
        }
        $state = Get-VmState
        if ($state -ne 'poweroff') {
            throw "RestoreSnapshot requires a powered-off VM (state=$state)."
        }
        $result = Invoke-VBox -Arguments @('snapshot', $VmName, 'restore', $SnapshotName)
        Write-Output $result.Output
    }
}
