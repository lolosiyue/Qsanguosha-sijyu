param(
    [Parameter(Mandatory = $true)]
    [string]$Executable,
    [string[]]$Arguments = @(),
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$QtRoot = 'H:\Qt6111\6.11.1\msvc2022_64',
    [string]$WorkingDirectory = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'
$QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
$qtBin = Join-Path $QtRoot 'bin'
$qtPlugins = Join-Path $QtRoot 'plugins'
$coreDll = if ($Configuration -eq 'Debug') { 'Qt6Cored.dll' } else { 'Qt6Core.dll' }
if (-not (Test-Path -LiteralPath (Join-Path $qtBin $coreDll) -PathType Leaf)) {
    throw "Qt runtime missing: $(Join-Path $qtBin $coreDll)"
}
$program = (Get-Command $Executable -CommandType Application -ErrorAction Stop).Source
$startInfo = New-Object System.Diagnostics.ProcessStartInfo
$startInfo.FileName = $program
$startInfo.WorkingDirectory = (Resolve-Path -LiteralPath $WorkingDirectory).Path
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$startInfo.RedirectStandardOutput = $true
$startInfo.RedirectStandardError = $true

# Some launchers provide both PATH and Path. Python normalizes the keys and can
# retain the stale value. Build one case-insensitive child environment instead.
$startInfo.EnvironmentVariables.Clear()
foreach ($entry in [Environment]::GetEnvironmentVariables().GetEnumerator()) {
    if ($entry.Key -ine 'PATH' -and $entry.Key -ine 'QT_PLUGIN_PATH') {
        $startInfo.EnvironmentVariables[$entry.Key] = $entry.Value
    }
}
$startInfo.EnvironmentVariables['PATH'] = "$qtBin;$env:PATH"
$startInfo.EnvironmentVariables['QT_PLUGIN_PATH'] = $qtPlugins

if ($null -ne $startInfo.PSObject.Properties['ArgumentList']) {
    foreach ($argument in $Arguments) { $startInfo.ArgumentList.Add($argument) }
} else {
    # Windows PowerShell 5.1 needs CRT quoting: escape quotes and double trailing
    # backslashes before the closing quote, including empty and spaced arguments.
    $startInfo.Arguments = ($Arguments | ForEach-Object {
        $escaped = [regex]::Replace($_, '(\\*)"', '$1$1\"')
        $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
        '"' + $escaped + '"'
    }) -join ' '
}
$process = [Diagnostics.Process]::Start($startInfo)
try {
    # Forward both pipes as lines arrive; do not buffer a whole test run or block
    # on one pipe while a verbose child fills the other pipe.
    $stdout = $process.StandardOutput.ReadLineAsync()
    $stderr = $process.StandardError.ReadLineAsync()
    while ($null -ne $stdout -or $null -ne $stderr) {
        $received = $false
        if ($null -ne $stdout -and $stdout.IsCompleted) {
            $line = $stdout.GetAwaiter().GetResult()
            if ($null -eq $line) { $stdout = $null } else {
                Write-Output $line
                $stdout = $process.StandardOutput.ReadLineAsync()
            }
            $received = $true
        }
        if ($null -ne $stderr -and $stderr.IsCompleted) {
            $line = $stderr.GetAwaiter().GetResult()
            if ($null -eq $line) { $stderr = $null } else {
                Write-Output $line
                $stderr = $process.StandardError.ReadLineAsync()
            }
            $received = $true
        }
        if (-not $received) { Start-Sleep -Milliseconds 20 }
    }
    $process.WaitForExit()
    $runExit = $process.ExitCode
} finally {
    $process.Dispose()
}
exit $runExit
