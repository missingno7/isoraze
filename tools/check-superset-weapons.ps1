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
$runRoot = [IO.Path]::GetFullPath((Join-Path $OutputDirectory ('weapons-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfff'))))
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
$mapPath = Join-Path $runRoot 'navigation.map'
& $PythonPath (Join-Path $PSScriptRoot 'make-superset-navigation.py') $mapPath
if ($LASTEXITCODE -ne 0) { throw 'Fixture generation failed.' }
function Read-Snapshot([string]$Root, [string]$Name) {
    $zip = [IO.Compression.ZipFile]::OpenRead((Join-Path $Root "weapons-$Name.dsave"))
    try {
        $reader = [IO.StreamReader]::new($zip.GetEntry('session.json').Open())
        try { $data = $reader.ReadToEnd() | ConvertFrom-Json -AsHashtable } finally { $reader.Dispose() }
    } finally { $zip.Dispose() }
    $player = @($data.objects | Where-Object { $_.classtype -eq 'DukePlayer' -and $_.Contains('actor') -and $null -ne $_.actor })[0]
    $blood = $data.objects[$player.bloodcombat]
    return @{ Data = $data; Player = $player; Blood = $blood; Body = $data.objects[$player.actor]; State = $data.objects[$blood.actor] }
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
    $commands = @('wait 90', 'superset_navigation pose 2', 'wait 4', 'save weapons-start "Duke weapon startup"', 'wait 2')
    if ($player -eq 'caleb') {
        $commands += @('superset_incoming_check', 'superset_movement_check start', '+Jump', 'wait 30', '-Jump', 'wait 40',
            '+Crouch', 'wait 30', '+Move_Forward', 'wait 30', '-Move_Forward', '-Crouch', 'wait 30', 'superset_movement_check')
    }
    foreach ($pickup in @('shotgun', 'bodyarmor')) {
        $commands += @('superset_navigation pose 2', 'wait 4', "superset_spawn_pickup $pickup", '+Move_Forward', 'wait 30', '-Move_Forward', 'wait 30')
    }
    $commands += @('save weapons-collected "Blood pickups with Duke weapon selected"', 'wait 2',
        'wait 30', '+Fire', 'wait 30', '-Fire', 'wait 40', 'save weapons-pistol "Native pistol ammo"', 'wait 2', 'screenshot',
        'superset_weapons blood', 'wait 90', 'slot 3', 'wait 100', 'save weapons-blood "Blood shotgun ready"', 'wait 2',
        '+Alt_Fire', 'wait 2', 'superset_weapons duke', 'save weapons-pending "Native shotgun action pending handoff"', 'wait 1',
        '-Alt_Fire', 'wait 120', 'save weapons-duke "Shot and reload completed before handoff"', 'wait 2',
        'wait 30', '+Fire', 'wait 30', '-Fire', 'wait 40', 'save weapons-nativeagain "Native pistol after handoff"', 'wait 2',
        'superset_weapons blood', 'wait 100', 'save weapons-bloodagain "Stowed Blood shotgun restored"', 'wait 2', 'screenshot',
        'superset_weapons duke', 'wait 30', 'map navigation.map', 'wait 60', 'load weapons-bloodagain', 'wait 4', 'gc full',
        'save weapons-loaded "Selected family and both inventories restored"', 'wait 2',
        'load weapons-pending', 'wait 4', 'gc full', 'wait 120', 'save weapons-replayed "Pending native handoff restored"', 'wait 2', 'quit')
    Write-SupersetCheckScript -Commands $commands -ScriptPath $script
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $ExePath
    $start.WorkingDirectory = Split-Path -Parent $ExePath
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    foreach ($arg in @('-iwad', $DukeGrp, '-superset', '-blooddata', $BloodDirectory, '-superset-player', $player,
        '-superset-weapons', 'duke', '-noautoload', '-noautoexec', '-nologo', '-nomonsters', '-nomusic', '-width', '800', '-height', '600',
        '-config', $config, '-savedir', $caseRoot, '-shotdir', $caseRoot, '-file', $mapPath, '-map', 'navigation.map',
        '+logfile', $log, '+iso_debug', '0', '+exec', $script)) { $start.ArgumentList.Add($arg) }
    if (!$EnableSound) { $start.ArgumentList.Add('-nosound') }
    $process = [Diagnostics.Process]::Start($start)
    try {
        if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
            $process.Kill(); $process.WaitForExit(); throw "$player timed out; inspect $log"
        }
        if ($process.ExitCode -ne 0) { throw "$player exited $($process.ExitCode); inspect $log" }
    } finally { $process.Dispose() }
    $states = @{}
    foreach ($name in @('start', 'collected', 'pistol', 'blood', 'pending', 'duke', 'nativeagain', 'bloodagain', 'loaded', 'replayed')) {
        $states[$name] = Read-Snapshot $caseRoot $name
        $s = $states[$name]
        $caleb = $player -eq 'caleb'
        if ([bool]$s.Player.bloodmovement -ne $caleb -or ($s.State.sprite.cstat -band 257) -or ($s.Body.sprite.cstat -band 257) -ne 257 -or
            ($caleb -and ($s.Body.viewzoffset -gt -50 -or [Math]::Abs($s.Body.clipdist - 12) -gt .01)) -or
            (!$caleb -and [Math]::Abs($s.Body.viewzoffset + 40) -gt .01)) {
            throw "$player $name changed character movement/size or introduced a second collider; inspect $log"
        }
    }
    foreach ($name in @('start', 'collected', 'pistol', 'duke', 'nativeagain', 'replayed')) {
        if ($states[$name].Player['superset.weapon_mode'] -ne 0) { throw "$player $name did not select Duke weapons" }
    }
    foreach ($name in @('blood', 'pending', 'bloodagain', 'loaded')) {
        if ($states[$name].Player['superset.weapon_mode'] -ne 1) { throw "$player $name did not retain Blood weapons" }
    }
    if ($states.collected.Blood.ammocount[2] -ne 8 -or !$states.collected.Blood.hasweapon[3] -or
        $states.collected.Blood.armor[0] -ne 1600 -or $states.collected.Player.curr_weapon -ne 1) { throw "$player inactive-family pickup failed" }
    if ($states.pistol.Player.ammo_count[1] -ge $states.collected.Player.ammo_count[1] -or
        $states.pistol.Blood.ammocount[2] -ne 8 -or $states.blood.Blood.curweapon -ne 3) { throw "$player native pistol/independent Blood ammo failed" }
    if ($states.pending.Data['superset.gamestate'].weapon_profile -ne 2 -or $states.pending.Blood.weapontimer -le 0 -or
        $states.duke.Blood.ammocount[2] -ne 6 -or $states.duke.Player.ammo_count[1] -ne $states.pistol.Player.ammo_count[1]) {
        throw "$player did not finish the native alternate shotgun action before handing off; inspect $log"
    }
    if ($states.nativeagain.Player.ammo_count[1] -ge $states.duke.Player.ammo_count[1] -or $states.nativeagain.Blood.ammocount[2] -ne 6 -or
        $states.bloodagain.Blood.curweapon -ne 3 -or $states.bloodagain.Blood.ammocount[2] -ne 6 -or
        $states.bloodagain.Player.ammo_count[1] -ne $states.nativeagain.Player.ammo_count[1]) { throw "$player inventory/selection was reset by a family change" }
    foreach ($pair in @(@('loaded', 'bloodagain'), @('replayed', 'duke'))) {
        $actual = $states[$pair[0]]; $expected = $states[$pair[1]]
        if (($actual.Blood.ammocount -join ',') -ne ($expected.Blood.ammocount -join ',') -or
            ($actual.Player.ammo_count -join ',') -ne ($expected.Player.ammo_count -join ',') -or
            ($actual.Blood.hasweapon -join ',') -ne ($expected.Blood.hasweapon -join ',') -or
            ($actual.Blood.armor -join ',') -ne ($expected.Blood.armor -join ',') -or
            $actual.Data['superset.gamestate'].weapon_profile -ne $expected.Data['superset.gamestate'].weapon_profile) {
            throw "$player $($pair[0]) replayed/refunded an action or lost a saved inventory/profile; inspect $log"
        }
    }
    if ($player -eq 'caleb' -and !(Select-String -LiteralPath $log -SimpleMatch 'Superset movement check: native profile=PASS, move/jump/crouch=PASS, one collider=PASS, clock=PASS.')) {
        throw 'Caleb movement was not exercised while Duke weapons were active.'
    }
    if ($player -eq 'caleb' -and !(Select-String -LiteralPath $log -SimpleMatch 'Superset incoming check: physical=PASS, armor=PASS, blast=PASS, immunity=PASS, god=PASS, resistance=PASS, fractions=PASS, impulse=PASS, laser=PASS, mapping=PASS.')) {
        throw 'Caleb incoming damage/armor changed with Duke weapons active.'
    }
    Write-Output "${player}: PASS (independent character/weapons, inactive pickups, native ammo, deferred handoff, saved action and both inventories)"
}
Write-Output "Artifacts: $runRoot"
