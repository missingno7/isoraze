#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$DukeGrp,
    [Parameter(Mandatory)][string]$BloodRff,
    [string]$ExePath = (Join-Path $PSScriptRoot '../build-mashup-dev/Release/raze.exe'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../build-mashup-dev/checks'),
    [string[]]$Profiles = @('E1L1-caleb','E1L1-duke','E3M1-caleb'),
    [string[]]$WeaponNames = @(),
    [string[]]$EnemyKinds = @('duke','blood'),
    [string[]]$Actions = @('primary','alternate'),
    [string]$ExistingRun = ''
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'superset-check-common.ps1')
$ExePath = (Resolve-Path -LiteralPath $ExePath).Path
$DukeGrp = (Resolve-Path -LiteralPath $DukeGrp).Path
$BloodRff = (Resolve-Path -LiteralPath $BloodRff).Path
$root = [IO.Path]::GetFullPath((Join-Path $OutputDirectory ('blood-arsenal-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfff'))))
if ($ExistingRun) { $root = (Resolve-Path -LiteralPath $ExistingRun).Path }
function Read-State([string]$Path) {
    $zip = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $reader = [IO.StreamReader]::new($zip.GetEntry('session.json').Open())
        try { return $reader.ReadToEnd() | ConvertFrom-Json -AsHashtable } finally { $reader.Dispose() }
    } finally { $zip.Dispose() }
}
function Inventory($Data) { return @($Data.objects | Where-Object classtype -eq 'GameplayInventory')[0] }
function Save-Path([string]$Directory, [string]$Name) {
    # Match the engine: names with an explicit extension are kept verbatim.
    if (![IO.Path]::HasExtension($Name)) { $Name += '.dsave' }
    return Join-Path $Directory $Name
}
function Target-Health($Data) {
    $actor = @($Data.objects | Where-Object { $_.sprite.owner -eq 31041 })[0]
    if (!$actor) { return 0 }
    if ($actor.classtype -eq 'BloodActor') { return [int]$actor.xsprite.health }
    return [int]$actor.sprite.extra * 16 - [int]$actor['superset.damage_remainder']
}
$weapons = @(
    @{name='napalm';bank=5;ammo=4}, @{name='dynamite';bank=6;ammo=5}, @{name='spraycan';bank=7;ammo=6},
    @{name='tesla';bank=8;ammo=7}, @{name='lifeleech';bank=9;ammo=8}, @{name='voodoo';bank=10;ammo=9},
    @{name='proximity';bank=11;ammo=10}, @{name='remote';bank=12;ammo=11})
if ($WeaponNames.Count) { $weapons = @($weapons | Where-Object { $_.name -in $WeaponNames }) }
foreach ($profile in $Profiles) {
    $map,$character = $profile.Split('-')
    $caseRoot = Join-Path $root $profile
    if (!$ExistingRun) { New-Item -ItemType Directory -Path $caseRoot -Force | Out-Null }
    $config = Join-Path $caseRoot 'raze.ini'; $script = Join-Path $caseRoot 'check.cfg'; $log = Join-Path $caseRoot 'raze.log'
    if (!$ExistingRun) { @'
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
'@ | Set-Content -LiteralPath $config }
    $commands = @('wait 100','god','superset_projectile_check isolate','wait 4','superset_duke_projectile_check flatarena',
        'wait 8','superset_duke_projectile_check faceclear','superset_pickup_switch 0','superset_projectile_check equipall','wait 12',
        'superset_arsenal_check give','save ready')
    foreach ($kind in $EnemyKinds) {
        foreach ($weapon in $weapons) {
            foreach ($action in $Actions) {
                $label = "$kind-$($weapon.name)-$action"
                $button = if ($action -eq 'primary') { 'Fire' } else { 'Alt_Fire' }
                $commands += @('load ready','wait 4',"superset_weapon_bank $($weapon.bank) blood.$($weapon.name)",
                    "slot $($weapon.bank)",'wait 80',"superset_projectile_check target $kind",'wait 4')
                if ($weapon.name -in @('dynamite','proximity','remote') -or ($weapon.name -in @('spraycan','napalm') -and $action -eq 'alternate')) {
                    $pitch = if ($weapon.name -eq 'proximity') {70} else {45}
                    $commands += @("superset_projectile_check pitch $pitch",'wait 4')
                }
                $commands += @("save $label-before",
                    "+$button",'wait 16',"-$button",'wait 28',"save $label-live")
                if ($weapon.name -eq 'remote') { $commands += @('+Fire','wait 12','-Fire') }
                if ($weapon.name -in @('dynamite','spraycan') -and $action -eq 'alternate') {
                    # Native lit fuse: release alt, then press/release fire to throw.
                    $commands += @('+Fire','wait 4','-Fire')
                }
                $settle = if ($weapon.name -in @('dynamite','spraycan') -and $action -eq 'alternate') { 150 } else { 100 }
                $commands += @("wait $settle", "save $label-hit", "load $label-live",'wait 8','gc full')
                if ($weapon.name -eq 'remote') { $commands += @('+Fire','wait 12','-Fire') }
                if ($weapon.name -in @('dynamite','spraycan') -and $action -eq 'alternate') { $commands += @('+Fire','wait 4','-Fire') }
                $commands += @("wait $settle", "save $label-restored")
            }
        }
    }
    $handoffs = @()
    foreach ($weapon in @($weapons | Where-Object { $_.name -in @('dynamite','spraycan','proximity','remote') })) {
        foreach ($destination in @('blood.shotgun','duke.shotgun')) {
            $label = "handoff-$($weapon.name)-$destination"
            $handoffs += @{label=$label;weapon=$weapon;selected=$(if ($destination -eq 'blood.shotgun') {35} else {3})}
            $button = if ($weapon.name -eq 'spraycan') {'Alt_Fire'} else {'Fire'}
            $commands += @('load ready','wait 4',"superset_weapon_bank $($weapon.bank) blood.$($weapon.name)","slot $($weapon.bank)",
                'wait 80', "+$button",'wait 12',"superset_weapon_bank 3 $destination",'slot 3','wait 4',"save $label-pending",
                "-$button")
            if ($weapon.name -eq 'spraycan') { $commands += @('wait 2','+Fire','wait 4','-Fire') }
            $commands += @('wait 100',"save $label-finished", "load $label-pending",'gc full',"-$button")
            if ($weapon.name -eq 'spraycan') { $commands += @('wait 2','+Fire','wait 4','-Fire') }
            $commands += @('wait 100',"save $label-restored")
        }
    }
    $commands += @('quit')
    if (!$ExistingRun) {
        Write-SupersetCheckScript -Commands $commands -ScriptPath $script
        $start = [Diagnostics.ProcessStartInfo]::new()
        $start.FileName = $ExePath; $start.WorkingDirectory = Split-Path -Parent $ExePath
        $start.UseShellExecute = $false; $start.CreateNoWindow = $true; $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
        $worldArgs = if ($map -eq 'E1L1') { @('-iwad',$DukeGrp,'-superset','-blooddata',(Split-Path -Parent $BloodRff),'-map',$map) }
            else { @('-iwad',$BloodRff,'-superset','-dukedata',$DukeGrp,'-map',$map) }
        foreach ($arg in ($worldArgs + @('-superset-player',$character,'-superset-weapons','blood',
            '-noautoload','-noautoexec','-nomonsters','-nomusic','-config',$config,'-savedir',$caseRoot,
            '+iso_debug','0','+logfile',$log,'+exec',$script))) { $start.ArgumentList.Add($arg) }
        $process = [Diagnostics.Process]::Start($start)
        try {
            if (!$process.WaitForExit(600000)) { $process.Kill(); $process.WaitForExit(); throw "$profile timed out; inspect $log" }
            if ($process.ExitCode -ne 0) { throw "$profile exited $($process.ExitCode); inspect $log" }
        } finally { $process.Dispose() }
    }
    if ((Get-Content -LiteralPath $log -Raw) -match '\bFAIL\b|VM execution aborted') { throw "Runtime failure; inspect $log" }
    $ready = Inventory (Read-State (Join-Path $caseRoot 'ready.dsave'))
    foreach ($index in 1..12) { if (!$ready.blood_weapons[$index]) { throw "$profile`: pickup did not grant Blood weapon $index" } }
    foreach ($kind in $EnemyKinds) {
        foreach ($weapon in $weapons) {
            foreach ($action in $Actions) {
                $label = "$kind-$($weapon.name)-$action"
                $before = Read-State (Join-Path $caseRoot "$label-before.dsave")
                if ((Inventory $before).selected_weapon -ne (32 + $weapon.bank) -or (Inventory $before).pending_weapon) {
                    throw "$profile $label`: native input did not select the requested weapon bank"
                }
                if ((Target-Health $before) -le 0) { throw "$profile $label`: target did not survive fixture placement; inspect $caseRoot" }
                foreach ($phase in @('hit','restored')) {
                    $after = Read-State (Join-Path $caseRoot "$label-$phase.dsave")
                    if ((Target-Health $after) -ge (Target-Health $before)) { throw "$profile $label $phase`: no target damage; inspect $caseRoot" }
                    if ((Inventory $after).blood_ammo[$weapon.ammo] -ge (Inventory $before).blood_ammo[$weapon.ammo]) {
                        throw "$profile $label $phase`: no ammo consumption; inspect $caseRoot"
                    }
                }
            }
        }
    }
    foreach ($case in $handoffs) {
        $pending = Inventory (Read-State (Save-Path $caseRoot "$($case.label)-pending"))
        if ($pending.pending_weapon -ne $case.selected -or $pending.selected_weapon -ne (32 + $case.weapon.bank)) {
            throw "$profile $($case.label) did not preserve a pending selection during the active throw"
        }
        foreach ($phase in @('finished','restored')) {
            $after = Inventory (Read-State (Save-Path $caseRoot "$($case.label)-$phase"))
            $spent = if ($case.weapon.name -eq 'spraycan') {480} else {1}
            if ($after.selected_weapon -ne $case.selected -or $after.pending_weapon -or
                $after.blood_ammo[$case.weapon.ammo] -ne $pending.blood_ammo[$case.weapon.ammo] - $spent) {
                throw "$profile $($case.label) $phase did not finish exactly one native throw and select the requested shotgun"
            }
        }
    }
    Write-Output "$profile`: PASS ($($weapons.Count * $Actions.Count * $EnemyKinds.Count) attack cases; $($handoffs.Count) queued switches; ammo, live save/load/GC)"
}
Write-Output "Artifacts: $root"
