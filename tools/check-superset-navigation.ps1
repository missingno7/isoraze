#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$DukeGrp,
    [Parameter(Mandatory)][string]$BloodDirectory,
    [string]$PythonPath = 'python',
    [string]$ExePath = (Join-Path $PSScriptRoot '../build-mashup/Release/raze.exe'),
    [ValidateSet('duke', 'caleb')][string[]]$Players = @('duke', 'caleb'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../build-mashup/checks')
)
$ErrorActionPreference = 'Stop'
$ExePath = (Resolve-Path -LiteralPath $ExePath).Path
$DukeGrp = (Resolve-Path -LiteralPath $DukeGrp).Path
$BloodDirectory = (Resolve-Path -LiteralPath $BloodDirectory).Path
$runRoot = [IO.Path]::GetFullPath((Join-Path $OutputDirectory ('navigation-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfff'))))
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
$mapPath = Join-Path $runRoot 'navigation.map'
& $PythonPath (Join-Path $PSScriptRoot 'make-superset-navigation.py') $mapPath
if ($LASTEXITCODE -ne 0) { throw 'Fixture generation failed.' }
foreach ($player in $Players) {
    $caseRoot = Join-Path $runRoot $player
    New-Item -ItemType Directory -Path $caseRoot -Force | Out-Null
    $config = Join-Path $caseRoot 'raze.ini'
    $script = Join-Path $caseRoot 'check.cfg'
    $log = Join-Path $caseRoot 'raze.log'
    @'
[GlobalSettings]
vid_fullscreen=false
vid_preferbackend=1
vid_activeinbackground=true
i_pauseinbackground=false
vid_vsync=false
cl_nostartscreens=true
'@ | Set-Content -LiteralPath $config
    $checks = @(
        'wait 90',
        'superset_navigation pose 0', 'wait 4', '+Open', 'wait 2', '-Open', 'wait 30',
        '+Move_Forward', 'wait 30', '-Move_Forward', 'wait 4',
        'superset_navigation check door 2 128 0 none',
        'superset_navigation pose 3', 'wait 4', '+Open', 'wait 2', '-Open', 'wait 30',
        'superset_navigation check lift 3 96 0 ground',
        'superset_navigation pose 5', 'wait 4', 'superset_navigation check teleport 6 128 0 none',
        'superset_navigation pose 2', 'wait 30', 'superset_navigation check cooldown 2 128 0 cooldown',
        'superset_navigation pose 5', 'wait 4', 'superset_navigation check repeat 6 128 0 none',
        'superset_navigation pose 9', 'wait 4', '+Open', 'wait 2', '-Open', 'wait 24',
        'superset_navigation check locked 9 128 0 locked',
        'superset_navigation key', '+Open', 'wait 2', '-Open', 'wait 80',
        'superset_navigation check access 9 128 0 key',
        'superset_navigation pose 11', 'wait 4', 'superset_navigation check exitclock 11 128 0 clock',
        'superset_navigation pose 9', 'wait 4',
        'save navigation "Navigation checkpoint"', 'wait 15',
        'superset_navigation pose 2', 'wait 4', 'load navigation', 'wait 60',
        'superset_navigation check restored 9 128 0 key',
        'superset_navigation pose 7', 'wait 4', '+Crouch', 'wait 12', '-Crouch', 'wait 4',
        'superset_navigation check dive 8 256 128 none',
        '+Jump', 'wait 45', '-Jump', 'wait 4',
        'superset_navigation check surface 7 128 0 none',
        'screenshot', 'wait 5', 'quit'
    )
    $checks -join '; ' | Set-Content -LiteralPath $script
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $ExePath
    $start.WorkingDirectory = Split-Path -Parent $ExePath
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    foreach ($arg in @('-iwad', $DukeGrp, '-superset', '-blooddata', $BloodDirectory,
        '-superset-player', $player, '-noautoload', '-noautoexec', '-nologo', '-nosound', '-nomusic',
        '-width', '800', '-height', '600', '-config', $config, '-savedir', $caseRoot, '-shotdir', $caseRoot,
        '-file', $mapPath, '-map', 'navigation.map', '+logfile', $log, '+iso_debug', '0', '+exec', $script)) {
        $start.ArgumentList.Add($arg)
    }
    $process = [Diagnostics.Process]::Start($start)
    try {
        if (!$process.WaitForExit(60000)) {
            $process.Kill()
            $process.WaitForExit()
            throw "$player timed out; inspect $log"
        }
        if ($process.ExitCode -ne 0) { throw "$player exited $($process.ExitCode); inspect $log" }
    } finally { $process.Dispose() }
    $results = @(Select-String -LiteralPath $log -Pattern '^Superset navigation .*: (PASS|FAIL)')
    $results | ForEach-Object { Write-Output $_.Line }
    if ($results.Count -ne 11 -or ($results | Where-Object { $_.Line -match ': FAIL' })) {
        throw "$player navigation checks failed; inspect $log"
    }
    Write-Output "${player}: PASS (native map input, animations, portals and save/load)"
}
Write-Output "Artifacts: $runRoot"
