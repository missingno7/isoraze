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
. (Join-Path $PSScriptRoot 'superset-check-common.ps1')
$ExePath = (Resolve-Path -LiteralPath $ExePath).Path
$DukeGrp = (Resolve-Path -LiteralPath $DukeGrp).Path
$BloodDirectory = (Resolve-Path -LiteralPath $BloodDirectory).Path
$runRoot = [IO.Path]::GetFullPath((Join-Path $OutputDirectory ('enemies-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfff'))))
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
$mapPath = Join-Path $runRoot 'navigation.map'
& $PythonPath (Join-Path $PSScriptRoot 'make-superset-navigation.py') $mapPath
if ($LASTEXITCODE -ne 0) { throw 'Fixture generation failed.' }
function Read-State([string]$Path) {
    $archive = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $reader = [IO.StreamReader]::new($archive.GetEntry('session.json').Open())
        try { return (ConvertFrom-SupersetSnapshot ($reader.ReadToEnd())) } finally { $reader.Dispose() }
    } finally { $archive.Dispose() }
}
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
        'wait 90', 'superset_navigation pose 2', 'wait 4', 'superset_spawn_enemy zombie', 'wait 4',
        'save enemy-start "Native zombie start"', 'wait 75', 'superset_enemy_check',
        'save enemy-active "Native zombie active"', 'wait 2', 'screenshot', 'superset_enemy_check guard',
        'map navigation.map', 'wait 60', 'load enemy-active', 'wait 4', 'gc full', 'superset_enemy_check guard',
        'superset_enemy_check', 'save enemy-restored "Native zombie restored"', 'wait 2',
        'superset_enemy_check watchdeath', '+Fire', 'wait 240', '-Fire', 'wait 8', 'superset_enemy_check',
        'save enemy-dead "Native zombie defeated"', 'wait 2', 'screenshot',
        '+Move_Forward', 'wait 30', '-Move_Forward', 'wait 4', 'save enemy-remains "Native remains collision"', 'wait 2'
    )
    if ($player -eq 'caleb') {
        $commands += @('load enemy-start', 'wait 4', 'superset_enemy_check guard',
            'superset_spawn_pickup spraycan', '+Move_Forward', 'wait 30', '-Move_Forward', 'wait 60', 'slot 7', 'wait 90',
            'superset_enemy_check watchburn', '+Fire', 'wait 30', '-Fire', 'wait 2', 'superset_enemy_check',
            'save enemy-burning "Native burning zombie"', 'wait 2', 'screenshot',
            '+Fire', 'wait 120', '-Fire', 'wait 10', 'superset_enemy_check',
            'save enemy-burned "Native burning death"', 'wait 2', 'screenshot')
    }
    if ($player -eq 'duke') {
        $commands += @('load enemy-start', 'wait 4', 'superset_enemy_check guard', 'give weapons', 'wait 4', 'slot 5', 'wait 60',
            'save enemy-armed "Native RPG ready"', 'wait 2', '+Fire', 'wait 15', '-Fire', 'wait 60',
            'superset_enemy_check', 'save enemy-blast "Native RPG versus Blood enemy"', 'wait 2', 'screenshot')
    }
    foreach ($removal in @('remove', 'expire')) {
        $commands += @('load enemy-start', 'wait 4', "superset_enemy_check $removal", 'wait 8', 'gc full',
            ('save enemy-{0} "Native enemy cleanup"' -f $removal), 'wait 2')
    }
    $commands += @('wait 5', 'quit')
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
        '-file', $mapPath, '-map', 'navigation.map', '+logfile', $log, '+iso_debug', '0', '+exec', $script)) {
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
    $states = @{}
    foreach ($name in @('start', 'active', 'restored', 'dead')) { $states[$name] = Read-State (Join-Path $caseRoot "enemy-$name.dsave") }
    foreach ($name in @('start', 'active', 'restored')) {
        $controllers = @($states[$name].objects | Where-Object { $_['superset.enemy_controller'] })
        $bodies = @($states[$name].objects | Where-Object { $_.classtype -eq 'SupersetBloodEnemy' })
        if ($controllers.Count -ne 1 -or $bodies.Count -ne 1) { throw "$player $name did not retain one pawn/controller; inspect $log" }
        $controller = $controllers[0]; $body = $bodies[0]
        if (($body.sprite.cstat -band 257) -ne 257 -or ($controller.sprite.cstat -band 257) -ne 0 -or
            $states[$name].objects[$body['superset.blood_state']] -ne $controller -or
            $states[$name].objects[$controller['superset.world_actor']] -ne $body) {
            throw "$player $name has invalid ownership/collision routing; inspect $log"
        }
    }
    $initial = @($states.start.objects | Where-Object { $_['superset.enemy_controller'] })[0]
    $active = @($states.active.objects | Where-Object { $_['superset.enemy_controller'] })[0]
    $restored = @($states.restored.objects | Where-Object { $_['superset.enemy_controller'] })[0]
    $startPlayer = @($states.start.objects | Where-Object { $_.classtype -eq 'DukePlayer' -and $_.Contains('actor') -and $null -ne $_.actor })[0]
    $activePlayer = @($states.active.objects | Where-Object { $_.classtype -eq 'DukePlayer' -and $_.Contains('actor') -and $null -ne $_.actor })[0]
    $before = $states.start.objects[$startPlayer.actor].sprite.extra
    $after = $states.active.objects[$activePlayer.actor].sprite.extra
    if ([Math]::Abs($initial.sprite.pos[0] - $active.sprite.pos[0]) -lt 1 -or !$active.xsprite.Contains('target') -or
        $after -le 0 -or $after -ge $before -or $restored.xsprite.health -ne $active.xsprite.health -or !$restored.xsprite.Contains('target')) {
        throw "$player did not prove pursuit, melee damage and restored AI; inspect $log"
    }
    foreach ($name in @('active', 'restored')) {
        $controller = @($states[$name].objects | Where-Object { $_['superset.enemy_controller'] })[0]
        $target = $states[$name].objects[$controller.xsprite.target]
        $physicalPlayer = @($states[$name].objects | Where-Object { $_.classtype -eq 'DukePlayer' -and $_.Contains('actor') -and $null -ne $_.actor })[0]
        if ($target['superset.world_actor'] -ne $physicalPlayer.actor) { throw "$player $name AI target is not the physical player alias" }
    }
    $survivors = @($states.dead.objects | Where-Object { $_['superset.enemy_controller'] })
    if (($survivors | Where-Object { $_.sprite.statnum -eq 6 -and $_.xsprite.health -gt 0 }) -or $states.dead.statistics.levels[-1].killcount -ne 1 -or
        !(Select-String -LiteralPath $log -SimpleMatch 'Superset enemy native death sequence: PASS.')) {
        throw "$player did not defeat the native enemy with normal fire input; inspect $log"
    }
    $deadPlayer = @($states.dead.objects | Where-Object { $_.classtype -eq 'DukePlayer' -and $_.Contains('actor') -and $null -ne $_.actor })[0]
    $remains = Read-State (Join-Path $caseRoot 'enemy-remains.dsave')
    $movedPlayer = @($remains.objects | Where-Object { $_.classtype -eq 'DukePlayer' -and $_.Contains('actor') -and $null -ne $_.actor })[0]
    $oldPose = $states.dead.objects[$deadPlayer.actor].sprite.pos
    $newPose = $remains.objects[$movedPlayer.actor].sprite.pos
    $blockedRemains = @($remains.objects | Where-Object {
        ($_.classtype -eq 'SupersetBloodEnemy' -or $_['superset.enemy_controller']) -and ($_.sprite.cstat -band 257)
    })
    if ($newPose[0] - $oldPose[0] -lt 64 -or $blockedRemains.Count) {
        throw "$player could not move past the nonblocking native remains; inspect $log"
    }
    if ($player -eq 'duke' -and ($deadPlayer.curr_weapon -ne 1 -or $deadPlayer.ammo_count[1] -ge $startPlayer.ammo_count[1])) {
        throw 'Duke did not use his native pistol/ammunition.'
    }
    if ($player -eq 'caleb' -and $deadPlayer.ammo_count[1] -ne $startPlayer.ammo_count[1]) { throw 'Caleb consumed Duke pistol ammunition.' }
    if ($player -eq 'caleb') {
        $burning = Read-State (Join-Path $caseRoot 'enemy-burning.dsave')
        $burned = Read-State (Join-Path $caseRoot 'enemy-burned.dsave')
        $burner = @($burning.objects | Where-Object { $_['superset.enemy_controller'] })
        $remaining = @($burned.objects | Where-Object { $_['superset.enemy_controller'] -and $_.sprite.statnum -eq 6 -and $_.xsprite.health -gt 0 })
        if ($burner.Count -ne 1 -or $burner[0].xsprite.health -ge $initial.xsprite.health -or $burner[0].xsprite.burntime -le 0 -or
            !(Select-String -LiteralPath $log -SimpleMatch 'Superset enemy native burn transformation: PASS.') -or
            $remaining.Count -ne 0 -or $burned.statistics.levels[-1].killcount -ne 1) {
            throw "Native zombie burning transformation/death not established; inspect $log"
        }
    }
    if ($player -eq 'duke') {
        $armed = Read-State (Join-Path $caseRoot 'enemy-armed.dsave')
        $blast = Read-State (Join-Path $caseRoot 'enemy-blast.dsave')
        $beforeRpg = @($armed.objects | Where-Object { $_.classtype -eq 'DukePlayer' -and $_.Contains('actor') -and $null -ne $_.actor })[0]
        $afterRpg = @($blast.objects | Where-Object { $_.classtype -eq 'DukePlayer' -and $_.Contains('actor') -and $null -ne $_.actor })[0]
        $remaining = @($blast.objects | Where-Object { $_['superset.enemy_controller'] -and $_.sprite.statnum -eq 6 -and $_.xsprite.health -gt 0 })
        if ($afterRpg.curr_weapon -ne 4 -or $afterRpg.ammo_count[4] -ge $beforeRpg.ammo_count[4] -or $remaining.Count -ne 0 -or $blast.statistics.levels[-1].killcount -ne 1) {
            throw "Duke RPG versus native Blood receiver not established; inspect $log"
        }
    }
    foreach ($removal in @('remove', 'expire')) {
        $cleaned = Read-State (Join-Path $caseRoot "enemy-$removal.dsave")
        if (@($cleaned.objects | Where-Object { $_['superset.enemy_controller'] -or $_.classtype -eq 'SupersetBloodEnemy' }).Count) {
            throw "$player $removal retained an orphan pawn/controller after GC; inspect $log"
        }
    }
    Write-Output "${player}: PASS (native pursuit/melee, one collider, saved AI target, native weapons/death, nonblocking remains, cleanup)"
}
Write-Output "Artifacts: $runRoot"
