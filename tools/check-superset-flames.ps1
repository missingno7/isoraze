#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$DukeGrp,
    [Parameter(Mandatory)][string]$BloodDirectory,
    [string]$PythonPath = 'python',
    [string]$ExePath = (Join-Path $PSScriptRoot '../build-mashup/Release/raze.exe'),
    [ValidateSet('caleb', 'duke')][string[]]$Players = @('caleb', 'duke'),
    [switch]$EnableSound,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../build-mashup/checks')
)
$ErrorActionPreference = 'Stop'
$ExePath = (Resolve-Path -LiteralPath $ExePath).Path
$DukeGrp = (Resolve-Path -LiteralPath $DukeGrp).Path
$BloodDirectory = (Resolve-Path -LiteralPath $BloodDirectory).Path
$runRoot = [IO.Path]::GetFullPath((Join-Path $OutputDirectory ('flames-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfff'))))
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
    $commands = @(
        'wait 90', 'superset_navigation pose 2', 'wait 4',
        'superset_spawn_pickup spraycan', '+Move_Forward', 'wait 30', '-Move_Forward', 'wait 60',
        'slot 7', 'wait 90', 'superset_navigation pose 2', 'wait 4',
        'superset_flame_check seed', 'wait 4', '+Fire', 'wait 30', '-Fire', 'wait 4',
        'superset_flame_check fired', 'screenshot',
        'save flames "Shared burning"', 'wait 20', 'superset_flame_check clear',
        'superset_blood_difficulty 4', 'load flames', 'wait 4', 'gc full', 'superset_flame_check saved',
        'mashup_combat 0', 'wait 20', 'superset_flame_check ownership',
        'wait 660', 'superset_flame_check expired', 'superset_flame_check responses',
        'screenshot', 'mashup_combat 1', '+Move_Backward', 'wait 6', 'superset_flame_check motion', '-Move_Backward', 'wait 5', 'quit'
    )
    $commands -join '; ' | Set-Content -LiteralPath $script
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $ExePath
    $start.WorkingDirectory = Split-Path -Parent $ExePath
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    foreach ($arg in @('-iwad', $DukeGrp, '-superset', '-blooddata', $BloodDirectory,
        '-superset-player', $player, '-noautoload', '-noautoexec', '-nologo', '-nomonsters', '-nomusic',
        '-width', '800', '-height', '600', '-config', $config, '-savedir', $caseRoot, '-shotdir', $caseRoot,
        '-file', $mapPath, '-map', 'navigation.map', '+logfile', $log, '+iso_debug', '0', '+mashup_combat', '1', '+exec', $script)) {
        $start.ArgumentList.Add($arg)
    }
    if (!$EnableSound) { $start.ArgumentList.Add('-nosound') }
    $process = [Diagnostics.Process]::Start($start)
    try {
        if (!$process.WaitForExit(60000)) {
            $process.Kill(); $process.WaitForExit()
            throw "$player timed out; inspect $log"
        }
        if ($process.ExitCode -ne 0) { throw "$player exited $($process.ExitCode); inspect $log" }
    } finally { $process.Dispose() }
    $expected = @(
        'Superset flame setup: PASS',
        'Superset flame fired: input=PASS, ignition=PASS, health=PASS, impulse=PASS',
        'Superset flame save check: PASS.',
        'Superset flame ownership check: PASS.',
        'Superset flame expiry check: PASS.',
        'Superset flame responses: resistance=PASS, immunity=PASS, ignition gate=PASS, separate effect=PASS, CON death=PASS.'
        'Superset flame motion check: PASS.'
    )
    foreach ($line in $expected) {
        if (!(Select-String -LiteralPath $log -SimpleMatch $line)) {
            Get-Content -LiteralPath $log | Where-Object { $_ -match '^Superset flame' } | Write-Output
            throw "$player flame checks failed; inspect $log"
        }
    }
    Write-Output "${player}: PASS (native spray input/projectiles, shared burning, resistance, expiry, save-load and native CON death)"
}
Write-Output "Artifacts: $runRoot"
