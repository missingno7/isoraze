#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$DukeGrp,
    [Parameter(Mandatory)][string]$BloodDirectory,
    [string]$PythonPath = 'python',
    [string]$ExePath = (Join-Path $PSScriptRoot '../build-mashup/Release/raze.exe'),
    [ValidateSet('caleb', 'duke')][string[]]$Players = @('caleb', 'duke'),
    [switch]$EnableSound,
    [ValidateRange(30, 180)][int]$TimeoutSeconds = 90,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../build-mashup/checks')
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'superset-check-common.ps1')
$ExePath = (Resolve-Path -LiteralPath $ExePath).Path
$DukeGrp = (Resolve-Path -LiteralPath $DukeGrp).Path
$BloodDirectory = (Resolve-Path -LiteralPath $BloodDirectory).Path
$runRoot = [IO.Path]::GetFullPath((Join-Path $OutputDirectory ('inventory-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfff'))))
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
    # Each placed pickup starts beyond the native collection radius. Movement
    # and collection use normal input/physics; diagnostics only arrange/check.
    $commands = @(
        'wait 90', 'superset_inventory_check init', 'wait 60',
        'superset_inventory_check place shotgun', '+Move_Forward', 'wait 30', '-Move_Forward', 'wait 60',
        'slot 3', 'wait 80', 'superset_inventory_check collected shotgun',
        'superset_weapon_input_check start shotgun alternate', '+Alt_Fire', 'wait 30', '-Alt_Fire', 'wait 100', 'superset_weapon_input_check',
        'superset_weapon_input_check start shotgun primary', '+Fire', 'wait 30', '-Fire', 'wait 90', 'superset_weapon_input_check',
        'screenshot',
        'superset_inventory_check place tommy', '+Move_Forward', 'wait 30', '-Move_Forward', 'wait 60',
        'slot 4', 'wait 80', 'superset_inventory_check collected tommy',
        'superset_weapon_input_check start tommy primary', '+Fire', 'wait 45', '-Fire', 'wait 30', 'superset_weapon_input_check',
        'superset_weapon_input_check start tommy alternate', '+Alt_Fire', 'wait 30', '-Alt_Fire', 'wait 140', 'superset_weapon_input_check',
        'superset_inventory_check place essence', '+Move_Forward', 'wait 30', '-Move_Forward', 'wait 60', 'superset_inventory_check collected essence',
        'superset_inventory_check place bodyarmor', '+Move_Forward', 'wait 30', '-Move_Forward', 'wait 60', 'superset_inventory_check collected bodyarmor',
        'superset_inventory_check place medpouch', '+Move_Forward', 'wait 30', '-Move_Forward', 'wait 60', 'superset_inventory_check collected medpouch',
        'superset_inventory_check place medpouch', '+Move_Forward', 'wait 30', '-Move_Forward', 'wait 60', 'superset_inventory_check full',
        'superset_inventory_check place superarmor', 'superset_inventory_check snapshot',
        'save inventory "Native Blood inventory"', 'wait 20', 'superset_inventory_check clear', 'wait 10',
        'load inventory', 'wait 90', 'gc full', 'superset_inventory_check saved',
        '+Move_Forward', 'wait 30', '-Move_Forward', 'wait 60', 'superset_inventory_check collected superarmor',
        'screenshot', 'wait 5', 'quit'
    )
    # Reset to the same flat room before each placement. The stock E1L1 roof
    # cannot accommodate this many consecutive forward walks without falling.
    $commands = $commands | ForEach-Object {
        if ($_ -like 'superset_inventory_check place *') { 'superset_navigation pose 2'; 'wait 4' }
        $_
    }
    Write-SupersetCheckScript -Commands $commands -ScriptPath $script
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
        if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
            $process.Kill()
            $process.WaitForExit()
            throw "$player timed out; inspect $log"
        }
        if ($process.ExitCode -ne 0) { throw "$player exited $($process.ExitCode); inspect $log" }
    } finally { $process.Dispose() }
    $placements = @(Select-String -LiteralPath $log -Pattern '^Superset inventory placed .*: PASS\.')
    $pickups = @(Select-String -LiteralPath $log -Pattern '^Superset inventory collected .*: PASS')
    $fire = @(Select-String -LiteralPath $log -SimpleMatch -Pattern 'Superset weapon input check: vectors=PASS, Blood ammo=PASS, Duke hit=PASS, Duke ammo=PASS')
    if ($placements.Count -ne 7 -or $pickups.Count -ne 6 -or $fire.Count -ne 4 -or
        !(Select-String -LiteralPath $log -SimpleMatch -Pattern 'Superset inventory full check: PASS.') -or
        !(Select-String -LiteralPath $log -SimpleMatch -Pattern 'Superset inventory save check: PASS.')) {
        Get-Content -LiteralPath $log | Where-Object { $_ -match '^Superset (inventory|weapon input)' } | Write-Output
        throw "$player inventory checks failed; inspect $log"
    }
    Write-Output "${player}: PASS (native pickups, both gun fire modes, independent ammo, inventory/item save-load)"
}
Write-Output "Artifacts: $runRoot"
