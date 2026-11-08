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
$root = [IO.Path]::GetFullPath((Join-Path $OutputDirectory ('blood-combat-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfff'))))
function Read-State([string]$Path) {
    $zip = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $reader = [IO.StreamReader]::new($zip.GetEntry('session.json').Open())
        try { return $reader.ReadToEnd() | ConvertFrom-Json -AsHashtable } finally { $reader.Dispose() }
    } finally { $zip.Dispose() }
}
function Get-Target($Data) {
    $entity = @($Data.objects | Where-Object { $_.classtype -eq 'GameplayEntity' -and $_.archetype -eq 2 } | Sort-Object identity)[-1]
    if (!$entity) { throw 'Missing Pig Cop fixture' }
    if ($entity.actor -isnot [long]) {
        if (!$entity.death_emitted -or !$entity.kill_credited) { throw 'Target disappeared without a common death' }
        return @{ sprite = @{ extra = 0 } }
    }
    return $Data.objects[$entity.actor]
}
function Get-Health($Actor) { return [int]$Actor.sprite.extra * 16 - [int]$Actor['superset.damage_remainder'] }
foreach ($map in @('E1M1','E3M1','DukeReceivers')) {
    $caseRoot = Join-Path $root $map
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
    $commands = @('wait 100', 'god', 'superset_duke_projectile_check arena', 'wait 8', 'superset_duke_projectile_check faceclear',
        'superset_blood_combat_check isolate',
        'give weapons', 'give ammo', 'superset_weapon_bank 1 blood.beast blood.pitchfork', 'slot 1', 'wait 60',
        'slot 1', 'wait 60', 'save selectable',
        'slot 7', 'wait 100', 'save ready', 'superset_spawn duke.pigcop', 'save flame-before',
        '+Fire', 'wait 12', '-Fire', 'wait 8', 'save flame-hit', 'superset_blood_combat_check clear', 'wait 30', 'save flame-tick',
        'load flame-hit', 'superset_blood_combat_check clear', 'wait 30', 'gc full', 'save flame-restored',
        'load ready', 'wait 4', 'slot 8', 'wait 100', 'superset_spawn duke.pigcop', 'save tesla-before',
        '+Fire', 'wait 12', '-Fire', 'wait 12', 'save tesla-hit',
        'load ready', 'wait 4', 'slot 8', 'wait 100', 'superset_spawn duke.pigcop', 'save tesla-alt-before',
        '+Alt_Fire', 'wait 40', '-Alt_Fire', 'wait 12', 'save tesla-alt-hit',
        'load ready', 'wait 4', 'slot 10', 'wait 100', 'superset_spawn duke.pigcop', 'save voodoo-before',
        'wait 4', '+Fire', 'wait 20', '-Fire', 'wait 12', 'save voodoo-hit',
        'load ready', 'wait 4', 'slot 10', 'wait 100', 'superset_spawn duke.pigcop', 'save voodoo-alt-before',
        'wait 4', '+Alt_Fire', 'wait 40', '-Alt_Fire', 'wait 12', 'save voodoo-alt-hit',
        'load ready', 'wait 4', 'superset_spawn duke.pigcop', 'save blast-before',
        'superset_blood_combat_check blast', 'wait 30', 'save blast-hit',
        'slot 3', 'wait 100', 'save stowed', 'superset_level_check', 'quit')
    if ($map -eq 'DukeReceivers') { $commands = @('wait 100','god','superset_receiver_check','quit') }
    Write-SupersetCheckScript -Commands $commands -ScriptPath $script
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $ExePath; $start.WorkingDirectory = Split-Path -Parent $ExePath
    $start.UseShellExecute = $false; $start.CreateNoWindow = $true; $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $worldArgs = if ($map -eq 'DukeReceivers') { @('-iwad',$DukeGrp,'-superset','-blooddata',(Split-Path -Parent $BloodRff),'-map','E1L1') }
        else { @('-iwad',$BloodRff,'-superset','-dukedata',$DukeGrp,'-map',$map) }
    foreach ($arg in ($worldArgs + @('-superset-player','caleb','-superset-weapons','blood',
        '-noautoload','-noautoexec','-nomonsters','-nomusic','-config',$config,'-savedir',$caseRoot,
        '+iso_debug','0','+logfile',$log,'+exec',$script))) { $start.ArgumentList.Add($arg) }
    $process = [Diagnostics.Process]::Start($start)
    try {
        if (!$process.WaitForExit($TimeoutSeconds * 1000)) { $process.Kill(); $process.WaitForExit(); throw "$map timed out; inspect $log" }
        if ($process.ExitCode -ne 0) { throw "$map exited $($process.ExitCode); inspect $log" }
    } finally { $process.Dispose() }
    if ((Get-Content -LiteralPath $log -Raw) -match '\bFAIL\b|VM execution aborted') { throw "Runtime failure; inspect $log" }
    if ($map -eq 'DukeReceivers') {
        if ((Get-Content -LiteralPath $log -Raw) -notmatch 'Superset generic receivers: PASS') { throw "Missing receiver check; inspect $log" }
        Write-Output 'Duke receivers: PASS (Pig Cop, Trooper, Enforcer, Octabrain; all shared damage categories and receipts without import identity)'
        continue
    }
    $states = @{}
    foreach ($name in @('selectable','flame-before','flame-hit','flame-tick','flame-restored','tesla-before','tesla-hit','tesla-alt-before','tesla-alt-hit',
        'voodoo-before','voodoo-hit','voodoo-alt-before','voodoo-alt-hit','blast-before','blast-hit','stowed')) {
        $states[$name] = Read-State (Join-Path $caseRoot "$name.dsave")
    }
    $inventory = @($states.selectable.objects | Where-Object classtype -eq 'GameplayInventory')[0]
    if (!$inventory.blood_weapons[13] -or $inventory.selected_weapon -ne 33 -or $inventory.pending_weapon) { throw "Beast selection guard failed; inspect $caseRoot" }
    foreach ($attack in @('flame','tesla','tesla-alt','voodoo','voodoo-alt','blast')) {
        $before = Get-Target $states["$attack-before"]; $after = Get-Target $states["$attack-hit"]
        if ((Get-Health $after) -ge (Get-Health $before)) { throw "$map $attack did not damage Pig Cop; inspect $caseRoot" }
    }
    $burning = Get-Target $states['flame-hit']
    if ($burning['superset.burn_time'] -le 0) { throw "Flames did not ignite Pig Cop; inspect $caseRoot" }
    foreach ($name in @('flame-tick','flame-restored')) {
        $after = Get-Target $states[$name]
        if ((Get-Health $after) -ge (Get-Health $burning) -or $after['superset.burn_time'] -ge $burning['superset.burn_time']) {
            throw "Burning did not advance across $name; inspect $caseRoot"
        }
    }
    $inventory = @($states.stowed.objects | Where-Object classtype -eq 'GameplayInventory')[0]
    if ($inventory.selected_weapon -ne 35 -or $inventory.pending_weapon) { throw "Weapon failed to stow; inspect $caseRoot" }
    $beforeAmmo = @($states['voodoo-before'].objects | Where-Object classtype -eq 'GameplayInventory')[0].blood_ammo[9]
    $afterAmmo = @($states['voodoo-hit'].objects | Where-Object classtype -eq 'GameplayInventory')[0].blood_ammo[9]
    if ($afterAmmo -ge $beforeAmmo) { throw "Voodoo received no damage receipt for ammo use; inspect $caseRoot" }
    Write-Output "${map}: PASS (Beast selection guard, native spray/Tesla/Voodoo primary and alternate hits, TNT, burning and save/load, weapon stow)"
}
Write-Output "Artifacts: $root"
