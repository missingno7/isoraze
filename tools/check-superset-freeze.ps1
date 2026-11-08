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
$runRoot = [IO.Path]::GetFullPath((Join-Path $OutputDirectory ('freeze-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfff'))))
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
$mapPath = Join-Path $runRoot 'navigation.map'
& $PythonPath (Join-Path $PSScriptRoot 'make-superset-navigation.py') $mapPath
if ($LASTEXITCODE -ne 0) { throw 'Fixture generation failed.' }
function Read-State([string]$Path) {
    $archive = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $reader = [IO.StreamReader]::new($archive.GetEntry('session.json').Open())
        try { return $reader.ReadToEnd() | ConvertFrom-Json -AsHashtable } finally { $reader.Dispose() }
    } finally { $archive.Dispose() }
}
function Snapshot([string]$Root, [string]$Name) {
    $data = Read-State (Join-Path $Root "freeze-$Name.dsave")
    $body = @($data.objects | Where-Object { $_.classtype -eq 'SupersetBloodEnemy' })[0]
    $state = if ($body) { $data.objects[$body['superset.blood_state']] }
    $player = @($data.objects | Where-Object { $_.classtype -eq 'DukePlayer' -and $_.Contains('actor') -and $null -ne $_.actor })[0]
    $seq = if ($body) { @($data['superset.blood_controller'].sequences | Where-Object {
        $_.target.Contains('actor') -and $_.target.actor -eq $body['superset.blood_state']
    })[0] }
    return @{ Data = $data; Body = $body; State = $state; Player = $player; Seq = $seq }
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
    $commands = @('wait 90', 'superset_navigation pose 2', 'wait 4', 'superset_spawn_enemy zombie', 'wait 4',
        'superset_enemy_check guard', 'save freeze-start "Unfrozen native zombie"', 'wait 2')
    if ($player -eq 'duke') {
        $commands += @('give weapons', 'slot 10', 'wait 60', 'save freeze-armed "Native freezer ready"', 'wait 2',
            '+Fire', 'wait 30', '-Fire', 'wait 8')
    } else {
        # Caleb cannot yet select Duke's weapon inventory. Exercise the native
        # Duke ShootThis/projectile path explicitly, not fictitious freezer input.
        foreach ($shot in 1..6) { $commands += @('superset_freeze_check launch', 'wait 6') }
        $commands += 'wait 8'
    }
    # Stock FREEZERHURTOWNER is enabled. Isolate the AI-pause observation from
    # lingering native ricochets and hits queued during the preparation guard.
    $commands += @('superset_freeze_check quiet', 'superset_freeze_check vulnerable', 'save freeze-frozen "Native projectiles froze Blood zombie"', 'wait 2', 'screenshot',
        'wait 30', 'save freeze-held "Paused native brain and sequence"', 'wait 8',
        'load freeze-frozen', 'wait 4', 'superset_enemy_check guard', 'superset_freeze_check toss', 'wait 60',
        'save freeze-landed "Frozen native gravity landing"', 'wait 8',
        'map navigation.map', 'wait 60', 'load freeze-frozen', 'wait 4', 'gc full', 'superset_enemy_check guard',
        'save freeze-restored "Restored frozen native zombie"', 'wait 2', 'screenshot')
    if ($player -eq 'duke') { $commands += @('slot 2', 'wait 60') }
    else { $commands += @('+Move_Forward', 'wait 30', '-Move_Forward', 'wait 4') }
    $commands += @('+Fire', 'wait 30', '-Fire', 'wait 4', 'save freeze-shattered "Actual weapon shatter"', 'wait 2', 'screenshot',
        # The native heavy-hit knockdown can still have 360 clocks remaining
        # after thaw. Allow that sequence and standing up before expecting melee.
        'load freeze-frozen', 'wait 4', 'superset_enemy_check guard', 'wait 300', 'save freeze-thawed "Timed thaw and resumed AI"', 'wait 8',
        'load freeze-start', 'wait 4', 'superset_enemy_check guard', 'superset_freeze_check burn', 'wait 4',
        'save freeze-burning "Independent burning and paused first frame"', 'wait 2', 'screenshot',
        'superset_freeze_check thaw', 'wait 4', 'save freeze-burnresumed "Deferred first frame resumed"', 'wait 8',
        'load freeze-start', 'wait 4', 'superset_freeze_check responses', 'wait 4', 'quit')
    Write-SupersetCheckScript -Commands $commands -ScriptPath $script
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $ExePath
    $start.WorkingDirectory = Split-Path -Parent $ExePath
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    foreach ($arg in @('-iwad', $DukeGrp, '-superset', '-blooddata', $BloodDirectory, '-superset-player', $player,
        '-noautoload', '-noautoexec', '-nologo', '-nomonsters', '-nomusic', '-width', '800', '-height', '600',
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
    $frozen = Snapshot $caseRoot frozen
    $held = Snapshot $caseRoot held
    $restored = Snapshot $caseRoot restored
    foreach ($snapshot in @($frozen, $held, $restored)) {
        if (!$snapshot.Body -or $snapshot.Body['superset.freeze_time'] -le 0 -or $snapshot.State.xsprite.health -ne 1 -or
            ($snapshot.Body.sprite.cstat -band 257) -ne 257 -or ($snapshot.State.sprite.cstat -band 257) -or
            $snapshot.Body.sprite.pal -ne 1 -or $snapshot.Body['superset.freeze_source'] -ne $snapshot.Player.actor -or
            $snapshot.Data.statistics.levels[-1].killcount -ne 0) { throw "$player invalid freeze/ownership/one-collider state; inspect $log" }
    }
    foreach ($snapshot in @($held, $restored)) {
        if ($snapshot.Seq.seqid -ne $frozen.Seq.seqid -or $snapshot.Seq.frameindex -ne $frozen.Seq.frameindex -or
            $snapshot.Seq.timecounter -ne $frozen.Seq.timecounter -or $snapshot.State.xsprite.statetimer -ne $frozen.State.xsprite.statetimer -or
            $snapshot.Body['superset.freeze_time'] -ge $frozen.Body['superset.freeze_time']) {
            throw "$player frozen AI/SEQ advanced or timer did not survive loading; inspect $log"
        }
    }
    $before = $frozen.Data.objects[$frozen.Player.actor].sprite.extra
    if ($held.Data.objects[$held.Player.actor].sprite.extra -ne $before) { throw "$player took an attack during the frozen hold" }
    $landed = Snapshot $caseRoot landed
    if ($landed.Body['superset.freeze_time'] -le 0 -or [Math]::Abs($landed.Body.sprite.pos[2] - 128) -gt .1 -or
        $landed.Seq.frameindex -ne $frozen.Seq.frameindex -or
        !(Select-String -LiteralPath $log -SimpleMatch 'Superset frozen gravity: PASS')) {
        throw "$player frozen object did not retain native gravity while its SEQ remained paused; inspect $log"
    }
    if ($player -eq 'duke') {
        $armed = Snapshot $caseRoot armed
        if ($frozen.Player.curr_weapon -ne 9 -or $frozen.Player.ammo_count[9] -ge $armed.Player.ammo_count[9]) { throw 'Native Duke freezer input/ammo not established.' }
    }
    $shattered = Snapshot $caseRoot shattered
    if ($shattered.Body -or @($shattered.Data.objects | Where-Object { $_['superset.enemy_controller'] }).Count -or
        $shattered.Data.statistics.levels[-1].killcount -ne 1 -or
        !@($shattered.Data.objects | Where-Object { $_.classtype -like 'DukeGlassPieces*' }).Count) { throw "$player did not shatter with one kill and native ice fragments; inspect $log" }
    $thawed = Snapshot $caseRoot thawed
    if (!$thawed.Body -or $thawed.Body['superset.freeze_time'] -gt 0 -or $null -ne $thawed.Body['superset.freeze_source'] -or
        $thawed.State.xsprite.health -ne 1 -or $thawed.Body.sprite.pal -ne $thawed.State.sprite.pal -or
        $thawed.Data.statistics.levels[-1].killcount -ne 0 -or $thawed.State.xsprite.aistate -eq $frozen.State.xsprite.aistate -or
        !$thawed.State.xsprite.Contains('target')) {
        throw "$player did not thaw alive and resume its native brain after knockdown; inspect $log"
    }
    $burning = Snapshot $caseRoot burning
    $resumed = Snapshot $caseRoot burnresumed
    if ($burning.Body['superset.freeze_time'] -le 0 -or $burning.State.sprite.lotag -ne 241 -or $burning.State.xsprite.burntime -le 0 -or
        !$burning.Seq.pendingupdate -or $resumed.Body['superset.freeze_time'] -gt 0 -or $resumed.Seq.pendingupdate -or
        $resumed.Body.sprite.picnum -eq $burning.Body.sprite.picnum) { throw "$player did not preserve independent burn and deferred SEQ resume; inspect $log" }
    if (!(Select-String -LiteralPath $log -SimpleMatch 'Superset freeze responses: PASS')) { throw "$player response checks failed; inspect $log" }
    Write-Output "${player}: PASS (native freeze projectiles, paused AI/SEQ, save/GC, shatter, timed thaw, independent Cold/fire responses)"
}
Write-Output "Artifacts: $runRoot"
