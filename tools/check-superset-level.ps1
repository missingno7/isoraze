#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$DukeGrp,
    [Parameter(Mandatory)][string]$BloodRff,
    [string]$ExePath = (Join-Path $PSScriptRoot '../build-mashup/Release/raze.exe'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../build-mashup/checks'),
    [ValidateRange(30,240)][int]$TimeoutSeconds = 150
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'superset-check-common.ps1')
$ExePath = (Resolve-Path -LiteralPath $ExePath).Path
$DukeGrp = (Resolve-Path -LiteralPath $DukeGrp).Path
$BloodRff = (Resolve-Path -LiteralPath $BloodRff).Path
$root = [IO.Path]::GetFullPath((Join-Path $OutputDirectory ('level-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfff'))))
function Read-State([string]$Path) {
    $zip = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $reader = [IO.StreamReader]::new($zip.GetEntry('session.json').Open())
        try { return $reader.ReadToEnd() | ConvertFrom-Json -AsHashtable } finally { $reader.Dispose() }
    } finally { $zip.Dispose() }
}
function Get-Entity($Data, [int]$Identity) {
    return @($Data.objects | Where-Object { $_.classtype -eq 'GameplayEntity' -and $_.identity -eq $Identity })[0]
}
function Get-Geometry($Data, [int]$Sector) {
    $geometry = $Data.engine.sectors[$Sector]
    # Save sectors are deltas against the input map. An absent coordinate is
    # stable across these snapshots, so compare serialized pairs directly.
    return @($geometry.floorz, $geometry.ceilingz) -join ','
}
function Get-Health($Data, [int]$Identity) {
    $entity = Get-Entity $Data $Identity
    if ($entity.actor -isnot [long]) { return 0 }
    $actor = $Data.objects[$entity.actor]
    if ($actor.classtype -eq 'BloodActor') { return [int]$actor.xsprite.health }
    return [int]$actor.sprite.extra * 16 - [int]$actor['superset.damage_remainder']
}
foreach ($format in @('blood','duke')) {
    $caseRoot = Join-Path $root $format
    New-Item -ItemType Directory -Path $caseRoot -Force | Out-Null
    $config = Join-Path $caseRoot 'raze.ini'; $script = Join-Path $caseRoot 'check.cfg'; $log = Join-Path $caseRoot 'raze.log'
    @'
[GlobalSettings]
vid_fullscreen=false
vid_preferbackend=1
vid_activeinbackground=true
i_pauseinbackground=false
vid_vsync=false
cl_nostartscreens=true
cl_noexitscreens=true
cl_loadingscreens=false
use_mouse=false
use_joystick=false
'@ | Set-Content -LiteralPath $config
    $map = if ($format -eq 'blood') { 'E1M1' } else { 'E1L1' }
    $commands = @('wait 100', 'superset_replacement 1', 'superset_replacement_seed 27', "map $map", 'wait 100', 'god')
    if ($format -eq 'blood') { $commands += @('superset_duke_projectile_check arena', 'wait 8') }
    $commands += @('superset_level_check fixture', 'wait 12', 'superset_level_check', 'screenshot', 'save before-death',
        'superset_arsenal_check give', 'slot 3', 'wait 20', '+Fire', 'wait 4', '-Fire', 'wait 30', 'save after-shot',
        'load before-death', 'wait 3',
        'superset_level_check kill', 'wait 1', 'save death-pending', 'wait 60', 'superset_level_check', 'save after-death',
        'load before-death', 'wait 3', 'superset_level_check kill', 'wait 60', 'save repeated-death',
        'load death-pending', 'wait 60', 'superset_level_check', 'save restored-death',
        'superset_level_check fixture pickup', 'save before-pickup', 'superset_level_check collect', 'wait 60',
        'superset_level_check', 'save after-pickup', 'load before-pickup', 'wait 3', 'superset_level_check collect',
        'wait 60', 'save repeated-pickup', 'superset_replacement 2', 'superset_replacement_seed 999', "map $map", 'wait 30',
        'load after-pickup', 'wait 6', 'gc full', 'superset_level_check', 'save restored-choices',
        "map $map", 'wait 30', 'save before-door', 'superset_level_check door', 'wait 20', 'save after-door', 'quit')
    Write-SupersetCheckScript -Commands $commands -ScriptPath $script
    $worldArgs = if ($format -eq 'blood') { @('-iwad',$BloodRff,'-superset','-dukedata',$DukeGrp,'-map',$map) }
        else { @('-iwad',$DukeGrp,'-superset','-blooddata',(Split-Path -Parent $BloodRff),'-map',$map) }
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $ExePath; $start.WorkingDirectory = Split-Path -Parent $ExePath
    $start.UseShellExecute = $false; $start.CreateNoWindow = $true; $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    foreach ($arg in ($worldArgs + @('-superset-player','caleb','-superset-weapons','blood',
        '-noautoload','-noautoexec','-nomonsters','-nomusic','-config',$config,'-savedir',$caseRoot,'-shotdir',$caseRoot,'+iso_debug','0','+logfile',$log,'+exec',$script))) { $start.ArgumentList.Add($arg) }
    $process = [Diagnostics.Process]::Start($start)
    try {
        if (!$process.WaitForExit($TimeoutSeconds * 1000)) { $process.Kill(); $process.WaitForExit(); throw "$format timed out; inspect $log" }
        if ($process.ExitCode -ne 0) { throw "$format exited $($process.ExitCode); inspect $log" }
    } finally { $process.Dispose() }
    $text = Get-Content -LiteralPath $log -Raw
    if ($text -match '\bFAIL\b|VM execution aborted|needs an? (environment |component )?adapter') { throw "Runtime failure; inspect $log" }
    $enemyMatch = [regex]::Match($text, 'source fixture: PASS \(identity=(\d+) source=enemy chosen=(\d+) event-sector=(\d+)')
    $pickupMatch = [regex]::Match($text, 'source fixture: PASS \(identity=(\d+) source=pickup chosen=(\d+) event-sector=(\d+)')
    if (!$enemyMatch.Success -or !$pickupMatch.Success) { throw "Missing imported fixtures; inspect $log" }
    $enemyId = [int]$enemyMatch.Groups[1].Value; $pickupId = [int]$pickupMatch.Groups[1].Value
    if ([int]$enemyMatch.Groups[2].Value -ne $(if ($format -eq 'blood') { 2 } else { 1 }) -or
        [int]$pickupMatch.Groups[2].Value -ne $(if ($format -eq 'blood') { 4 } else { 3 })) { throw "$format replacement direction failed" }
    $states = @{}
    foreach ($name in @('before-death','after-shot','death-pending','after-death','repeated-death','restored-death','before-pickup',
        'after-pickup','repeated-pickup','restored-choices','before-door','after-door')) { $states[$name] = Read-State (Join-Path $caseRoot "$name.dsave") }
    $initial = $states['before-death']; $entity = Get-Entity $initial $enemyId
    if ((Get-Health $states['after-shot'] $enemyId) -ge (Get-Health $initial $enemyId)) { throw "$format native shotgun did not hit the common target" }
    if ($entity.actor -eq $entity.source_node -or $entity.death_emitted) { throw "$format replacement has no separate source binding" }
    $body = $initial.objects[$entity.actor]; $node = $initial.objects[$entity.source_node]
    if ($body['superset.enemy_controller'] -or $body.Contains('superset.world_actor') -or $body.Contains('superset.blood_state') -or
        ($node.sprite.cstat -band 257)) { throw "$format migrated entity still has a paired gameplay body" }
    foreach ($name in @('after-death','repeated-death','restored-death')) {
        $state = $states[$name]; $entity = Get-Entity $state $enemyId
        if (!$entity.death_emitted -or !$entity.kill_credited -or $state.engine.maplocals.kills.got -ne $initial.engine.maplocals.kills.got + 1 -or
            $state.engine.maplocals.kills.max -ne $initial.engine.maplocals.kills.max) { throw "$format exactly-once death failed in $name" }
        if ($format -eq 'blood' -and (Get-Geometry $state ([int]$enemyMatch.Groups[3].Value)) -eq (Get-Geometry $initial ([int]$enemyMatch.Groups[3].Value))) {
            throw "Blood source death did not operate its native door in $name"
        }
    }
    $pickupBefore = $states['before-pickup']; $pickupEntity = Get-Entity $pickupBefore $pickupId
    foreach ($name in @('after-pickup','repeated-pickup','restored-choices')) {
        $state = $states[$name]; $taken = Get-Entity $state $pickupId
        if (!$taken.pickup_emitted) { throw "$format replacement pickup failed in $name" }
        $inventory = @($state.objects | Where-Object classtype -eq 'GameplayInventory')[0]
        $previous = @($pickupBefore.objects | Where-Object classtype -eq 'GameplayInventory')[0]
        if ($format -eq 'blood') {
            if (!$inventory.duke_weapons[2] -or $inventory.duke_ammo[2] -le $previous.duke_ammo[2]) { throw 'Duke replacement did not update common inventory' }
            $binding = $state['superset.level'].bindings[$taken.pickup_binding]
            if (!($state.objects[$binding.node].xsprite.flags -band 128)) { throw 'Original Blood pickup trigger was not retained' }
        } elseif (!$inventory.blood_weapons[3] -or $inventory.blood_ammo[2] -le $previous.blood_ammo[2]) { throw 'Blood replacement did not update common inventory' }
        if ($taken.archetype -ne $pickupEntity.archetype -or $taken.source_index -ne $pickupEntity.source_index -or
            $state['superset.level'].replacement -ne 1 -or $state['superset.level'].seed -ne 27) { throw 'Saved replacement choices were rerolled' }
    }
    $doorMatch = [regex]::Match($text, 'mechanism use: PASS \(sector=(\d+)')
    if (!$doorMatch.Success -or (Get-Geometry $states['before-door'] ([int]$doorMatch.Groups[1].Value)) -eq
        (Get-Geometry $states['after-door'] ([int]$doorMatch.Groups[1].Value))) { throw "$format common mechanism interaction did not move native geometry" }
    Write-Output "$format level: PASS (single body, replacement enemy/pickup, exactly-once kills, native mechanisms/events, saved choices and lifecycle)"
}
Write-Output "Artifacts: $root"
