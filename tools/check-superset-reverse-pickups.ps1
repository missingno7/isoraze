#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$DukeGrp,
    [Parameter(Mandatory)][string]$BloodRff,
    [string]$ExePath = (Join-Path $PSScriptRoot '../build-mashup/Release/raze.exe'),
    [ValidateRange(30, 240)][int]$TimeoutSeconds = 150,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../build-mashup/checks')
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'superset-check-common.ps1')
$ExePath = (Resolve-Path -LiteralPath $ExePath).Path
$DukeGrp = (Resolve-Path -LiteralPath $DukeGrp).Path
$BloodRff = (Resolve-Path -LiteralPath $BloodRff).Path
$runRoot = [IO.Path]::GetFullPath((Join-Path $OutputDirectory ('reverse-pickups-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfff'))))
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
function Run-Case([string]$Name, [string[]]$Commands, [string]$Con = '', [switch]$DukeMap) {
    $root = Join-Path $runRoot $Name
    New-Item -ItemType Directory -Path $root -Force | Out-Null
    $config = Join-Path $root 'raze.ini'; $script = Join-Path $root 'check.cfg'; $log = Join-Path $root 'raze.log'
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
    Write-SupersetCheckScript -Commands $Commands -ScriptPath $script
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $ExePath; $start.WorkingDirectory = Split-Path -Parent $ExePath
    $start.UseShellExecute = $false; $start.CreateNoWindow = $true; $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $worldArgs = if ($DukeMap) { @('-iwad', $DukeGrp, '-superset', '-blooddata', (Split-Path -Parent $BloodRff), '-map', 'E1L1') }
        else { @('-iwad', $BloodRff, '-superset', '-dukedata', $DukeGrp, '-map', 'E1M1') }
    foreach ($arg in ($worldArgs + @('-superset-player', 'caleb', '-superset-weapons', 'blood',
        '-noautoload', '-noautoexec', '-nologo', '-nomonsters', '-nomusic', '-config', $config, '-savedir', $root, '-shotdir', $root,
        '+logfile', $log, '+iso_debug', '0', '+exec', $script))) { $start.ArgumentList.Add($arg) }
    if ($Con) { $start.ArgumentList.Add('-dukecon'); $start.ArgumentList.Add($Con) }
    $process = [Diagnostics.Process]::Start($start)
    try {
        if (!$process.WaitForExit($TimeoutSeconds * 1000)) { $process.Kill(); $process.WaitForExit(); throw "$Name timed out; inspect $log" }
        if ($process.ExitCode -ne 0) { throw "$Name exited $($process.ExitCode); inspect $log" }
    } finally { $process.Dispose() }
    $text = Get-Content -LiteralPath $log -Raw
    if ($text -match '\bFAIL\b|VM execution aborted') { throw "$Name failed; inspect $log" }
    return $root
}
function Read-State([string]$Root, [string]$Name) {
    $zip = [IO.Compression.ZipFile]::OpenRead((Join-Path $Root "pickup-$Name.dsave"))
    try {
        $reader = [IO.StreamReader]::new($zip.GetEntry('session.json').Open())
        try { $data = $reader.ReadToEnd() | ConvertFrom-Json -AsHashtable } finally { $reader.Dispose() }
    } finally { $zip.Dispose() }
    $player = if ($data.Contains('superset.duke_program')) { $data.objects[$data['superset.duke_program'].players[0]] }
        else { @($data.objects | Where-Object { $_.classtype -eq 'DukePlayer' -and $_.Contains('actor') })[0] }
    $blood = @($data.objects | Where-Object { $_.classtype -eq 'BloodPlayer' -and $_.Contains('actor') })[0]
    $items = @($data.objects | Where-Object { $_['superset.foreign_fx'] -and $_.classtype -match '^Duke(Ammo|AmmoLots|Shotgunammo|BatteryAmmo|FirstgunSprite|ShotgunSprite|ChaingunSprite)$' })
    return @{ Data = $data; Duke = $player; Blood = $blood; Items = $items }
}
function Collect([string]$Kind) {
    return @('superset_duke_projectile_check arena', 'wait 4', "superset_spawn_duke_pickup $Kind", 'wait 10',
        '+Move_Forward', 'wait 30', '-Move_Forward', 'wait 15', "save pickup-$Kind `"Collected native Duke $Kind`"", 'wait 2')
}
$natural = Run-Case loadout (@('wait 100') + (Collect bullets) + @(
    'superset_weapons duke', 'wait 80', 'save pickup-switched "Ammo pickup grants no pistol"',
    'superset_weapons blood', 'wait 80', 'changemap E1M2', 'wait 60',
    'superset_weapons duke', 'wait 80', 'save pickup-nextmap "Empty gun ownership persists across maps"',
    'load pickup-switched', 'wait 12', 'gc full', 'save pickup-loadoutrestored "Restore collected ammo without free pistol"', 'quit'))
foreach ($name in @('bullets','switched','nextmap','loadoutrestored')) {
    $s = Read-State $natural $name
    if ($s.Duke['superset.foreign_start_with_weapons'] -or $s.Duke.ammo_count[1] -ne 12 -or
        @($s.Duke.gotweapon[1..16] | Where-Object { $_ }).Count -or $s.Duke.curr_weapon -ne 0) {
        throw "Blood starting loadout granted Duke weapons or lost collected ammo in $name."
    }
}
$empty = Run-Case empty @('wait 100', 'superset_weapons duke', 'wait 80',
    'save pickup-empty "First family switch does not grant a loadout"', 'quit')
$s = Read-State $empty empty
if (($s.Duke.ammo_count | Measure-Object -Sum).Sum -or @($s.Duke.gotweapon[1..16] | Where-Object { $_ }).Count -or $s.Duke.curr_weapon -ne 0) {
    throw 'First family switch granted an inactive Duke loadout.'
}
Write-Output 'Starting inventory: PASS (empty guns/ammo, ammo pickup before family switch, map and save persistence, no free pistol)'
$forward = Run-Case duke-map @('wait 100', 'save pickup-forwardstart "Blood weapons on Duke map start with empty Duke inventory"',
    'superset_weapons duke', 'wait 80', 'save pickup-forwardswitched "Family switch grants no pistol on Duke map"',
    'changemap E1L2', 'wait 100', 'save pickup-forwardnext "Normal Duke level transition grants no pistol"',
    'load pickup-forwardswitched', 'wait 12', 'gc full', 'save pickup-forwardrestored "Restore empty native Duke inventory"', 'quit') -DukeMap
foreach ($name in @('forwardstart','forwardswitched','forwardnext','forwardrestored')) {
    $s = Read-State $forward $name
    if (($s.Duke.ammo_count | Measure-Object -Sum).Sum -or @($s.Duke.gotweapon[1..16] | Where-Object { $_ }).Count -or $s.Duke.curr_weapon -ne 0) {
        throw "Blood starting loadout on Duke maps granted a gun or ammo in $name."
    }
}
Write-Output 'Duke-map starting inventory: PASS (Blood startup, family switch, native level transition and save/restore remain empty)'
$commands = @('wait 100', 'superset_duke_projectile_check arena', 'wait 8', 'superset_duke_pickup_check environment',
    'save pickup-start "Blood player and native empty Duke inventory"', 'wait 2',
    'superset_spawn_duke_pickup pistol', 'wait 10', 'save pickup-waiting "Duke item waits outside reach"', 'wait 2', 'screenshot',
    '+Move_Forward', 'wait 30', '-Move_Forward', 'wait 15', 'save pickup-pistol "Collected Duke pistol"', 'wait 2')
foreach ($kind in @('shotgun','chaingun','bullets','bulletbox','shells','battery')) { $commands += Collect $kind }
$commands += @('superset_duke_pickup_check full', 'wait 2')
foreach ($kind in @('pistol','shotgun','chaingun','bullets','bulletbox','shells','battery')) {
    $commands += @('superset_duke_projectile_check arena', 'wait 4', "superset_spawn_duke_pickup $kind", 'wait 10',
        '+Move_Forward', 'wait 30', '-Move_Forward', 'wait 15')
}
$commands += @('save pickup-full "Full ammo refuses all seven native pickups"', 'wait 2',
    'map E1M2', 'wait 60', 'load pickup-full', 'wait 8', 'gc full', 'save pickup-restored "Pickups and native inventory after map and GC"', 'wait 2',
    'superset_duke_pickup_check empty', 'superset_duke_projectile_check arena', 'wait 4',
    '+Move_Forward', 'wait 30', '-Move_Forward', 'wait 15', 'save pickup-recovered "Restored pickups become collectable"', 'wait 2', 'screenshot',
    'superset_weapons duke', 'wait 80', 'slot 3', 'wait 60', '+Fire', 'wait 30', '-Fire', 'wait 40',
    'save pickup-fired "Collected native shotgun fires"', 'quit')
$stock = Run-Case stock $commands
if (!(Select-String -LiteralPath (Join-Path $stock 'raze.log') -SimpleMatch 'Superset Duke pickup environment: PASS')) { throw 'Native pickup environment check was not exercised.' }
$start = Read-State $stock start
$waiting = Read-State $stock waiting
if ($waiting.Items.Count -ne 1 -or ($waiting.Duke.ammo_count[1..3] -join ',') -ne '0,0,0') { throw 'Stationary player collected a distant pickup.' }
$expected = @{
    pistol = @(48,0,0); shotgun = @(48,10,0); chaingun = @(48,10,50)
    bullets = @(60,10,50); bulletbox = @(108,10,50); shells = @(108,20,50); battery = @(108,20,100)
}
foreach ($kind in $expected.Keys) {
    $s = Read-State $stock $kind
    if (($s.Duke.ammo_count[1..3] -join ',') -ne ($expected[$kind] -join ',') -or $s.Items.Count -or
        $s.Duke['superset.weapon_mode'] -ne 1 -or $s.Blood.curweapon -ne $start.Blood.curweapon -or
        ($s.Blood.ammocount -join ',') -ne ($start.Blood.ammocount -join ',')) { throw "$kind native pickup or inactive inventory isolation failed; inspect $stock" }
    if ($s.Data.objects[$s.Duke.actor].sprite.cstat -band 257) { throw 'Foreign inventory introduced another player collider.' }
}
foreach ($name in @('full','restored')) {
    $s = Read-State $stock $name
    if ($s.Items.Count -ne 7 -or ($s.Duke.ammo_count[1..3] -join ',') -ne '200,50,200' -or
        @($s.Items | Where-Object { $_['superset.con_blocked'] -or ($_.sprite.cstat -band 257) }).Count) { throw "$name full-cap refusal/save failed; inspect $stock" }
}
$recovered = Read-State $stock recovered
$fired = Read-State $stock fired
if ($recovered.Items.Count -or ($recovered.Duke.ammo_count[1..3] -join ',') -ne '108,20,100' -or
    $fired.Duke.curr_weapon -ne 2 -or $fired.Duke.ammo_count[2] -ge 20) { throw "Restored pickup collection/firing failed; inspect $stock" }
Write-Output 'Stock Duke pickups: PASS (seven native CON actors, amounts/caps/refusal, inactive inventory, actual movement/firing, save/map/GC)'

$freshRoot = Join-Path $runRoot 'fresh'
New-Item -ItemType Directory -Path $freshRoot -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $stock 'pickup-full.dsave') -Destination $freshRoot
$fresh = Run-Case fresh @('wait 80', 'load pickup-full', 'wait 8', 'gc full',
    'save pickup-fresh "Fresh-process native pickup restoration"', 'quit')
$s = Read-State $fresh fresh
if ($s.Items.Count -ne 7 -or ($s.Duke.ammo_count[1..3] -join ',') -ne '200,50,200') { throw 'Fresh-process pickup restoration failed.' }

$con = Join-Path $runRoot 'pickup.con'
@'
include game.con
actor AMMO 0
  fall
  ifp palive ifcount 6 ifpdistl RETRIEVEDISTANCE ifcanseetarget
  {
    addammo PISTOL_WEAPON 7
    quote 65
    killit
  }
enda
'@ | Set-Content -LiteralPath $con
$mod = Run-Case mod (@('wait 100', 'superset_duke_projectile_check arena', 'wait 8', 'superset_duke_pickup_check init', 'superset_duke_pickup_check frail') + (Collect bullets) + @('quit')) $con
$s = Read-State $mod bullets
if ($s.Duke.ammo_count[1] -ne 7 -or $s.Items.Count -or $s.Data.objects[$s.Blood.actor].xsprite.health -ne 1 -or
    $s.Data.objects[$s.Duke.actor].sprite.extra -ne 1) { throw 'Modified native CON amount or fractional-health pickup failed.' }
Write-Output 'Duke pickup program: PASS (fresh process, modified native CON amount, player alive at one Q4 health quantum)'
@'
include game.con
actor AMMO 0
  fall
  operate
enda
'@ | Set-Content -LiteralPath $con
$blocked = Run-Case blocked @('wait 100', 'superset_duke_projectile_check arena', 'wait 8', 'superset_duke_pickup_check init',
    'superset_spawn_duke_pickup bullets', 'wait 15', 'save pickup-blocked "Unsupported Duke map opcode stopped"',
    'wait 2', 'map E1M2', 'wait 60', 'load pickup-blocked', 'wait 4', 'gc full',
    'save pickup-stillblocked "Stopped script survives restore"', 'quit') $con
foreach ($name in @('blocked','stillblocked')) {
    $s = Read-State $blocked $name
    if ($s.Items.Count -ne 1 -or !$s.Items[0]['superset.con_blocked'] -or $s.Duke.ammo_count[1] -ne 0) {
        throw 'Unsupported map command entered the native Blood world or lost its stopped state.'
    }
}
if (@(Select-String -LiteralPath (Join-Path $blocked 'raze.log') -SimpleMatch 'needs an adapter for CON command').Count -ne 1) { throw 'Missing or repeated unsupported-command diagnostic.' }
Write-Output 'Duke pickup boundary: PASS (unsupported map command stops before execution and stays stopped after save/GC)'
Write-Output "Artifacts: $runRoot"
