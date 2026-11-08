#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$DukeGrp,
    [Parameter(Mandatory)][string]$BloodRff,
    [string]$ExePath = (Join-Path $PSScriptRoot '../build-mashup/Release/raze.exe'),
    [switch]$EnableSound,
    [ValidateRange(30, 180)][int]$TimeoutSeconds = 90,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../build-mashup/checks')
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'superset-check-common.ps1')
$ExePath = (Resolve-Path -LiteralPath $ExePath).Path
$DukeGrp = (Resolve-Path -LiteralPath $DukeGrp).Path
$BloodRff = (Resolve-Path -LiteralPath $BloodRff).Path
$runRoot = [IO.Path]::GetFullPath((Join-Path $OutputDirectory ('duke-projectiles-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfff'))))
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
$config = Join-Path $runRoot 'raze.ini'
$script = Join-Path $runRoot 'check.cfg'
$log = Join-Path $runRoot 'raze.log'
@'
[GlobalSettings]
vid_fullscreen=false
vid_preferbackend=1
vid_activeinbackground=true
i_pauseinbackground=false
vid_vsync=false
cl_nostartscreens=true
'@ | Set-Content -LiteralPath $config
$commands = @('wait 90', 'superset_check', 'superset_duke_resource_check', 'superset_duke_projectile_check guard',
    'superset_duke_projectile_check arena', 'wait 8',
    'superset_duke_projectile_check environment',
    'superset_duke_projectile_check faceclear', 'superset_duke_projectile_check target', 'superset_duke_projectile_check seed', 'wait 2',
    'save projectile-flight "Duke laser travelling in native Blood world"', 'wait 4', 'screenshot', 'superset_duke_projectile_check status',
    'save projectile-held "Native projectile movement"', 'wait 4', 'map E1M2', 'wait 60', 'load projectile-flight', 'wait 1', 'gc full',
    'save projectile-restored "Restored generic source and projectile pose"', 'wait 1', 'superset_duke_projectile_check release',
    'superset_duke_projectile_check accelerate', 'wait 40',
    'superset_duke_projectile_check status', 'save projectile-hit "Native Blood damage from Duke projectile contact"', 'wait 30',
    'save projectile-quiet "One impact and expired laser trails"', 'wait 1',
    'superset_duke_projectile_check clear', 'wait 4', 'superset_duke_projectile_check faceclear',
    'superset_duke_projectile_check target', 'superset_duke_projectile_check offscreen', 'wait 30',
    'save projectile-offscreen "Unseen projectile and trail lifetime"', 'wait 1',
    'superset_duke_projectile_check clear', 'wait 4', 'superset_duke_projectile_check faceclear',
    'superset_duke_projectile_check thing', 'save projectile-thingbefore "Native Blood thing"', 'wait 1',
    'superset_duke_projectile_check trace', 'superset_duke_projectile_check release', 'superset_duke_projectile_check fast', 'wait 8',
    'save projectile-thingafter "Duke laser versus native Blood thing"', 'wait 1', 'superset_duke_projectile_check notrace',
    'superset_duke_projectile_check target', 'save projectile-playerbefore "Native Blood player before incoming projectile"', 'wait 1',
    'superset_duke_projectile_check player', 'wait 8', 'save projectile-playerafter "Native armor and incoming Duke projectile"', 'wait 1',
    'superset_duke_projectile_check clear', 'wait 4', 'superset_duke_projectile_check floor', 'wait 4',
    'save projectile-floor "Projectile floor contact"', 'wait 1', 'superset_duke_projectile_check wall',
    'wait 12', 'save projectile-wall "Projectile wall contact"', 'wait 1',
    'superset_duke_projectile_check killtarget', 'superset_duke_projectile_check watchdeath',
    'superset_duke_projectile_check release', 'superset_duke_projectile_check fast', 'wait 16',
    'save projectile-killed "Native death and source credit"', 'wait 1',
    'superset_duke_projectile_check clear', 'wait 4', 'gc full', 'superset_health_check', 'mashup_runtime_check',
    'save projectile-cleared "No foreign combat actors remain"', 'wait 1', 'quit')
Write-SupersetCheckScript -Commands $commands -ScriptPath $script
$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = $ExePath
$start.WorkingDirectory = Split-Path -Parent $ExePath
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
foreach ($arg in @('-iwad', $BloodRff, '-superset', '-dukedata', $DukeGrp, '-noautoload', '-noautoexec', '-nologo',
    '-nomonsters', '-nomusic', '-config', $config, '-savedir', $runRoot, '-shotdir', $runRoot, '-map', 'E1M1', '+logfile', $log,
    '+iso_debug', '0', '+exec', $script)) { $start.ArgumentList.Add($arg) }
if (!$EnableSound) { $start.ArgumentList.Add('-nosound') }
$process = [Diagnostics.Process]::Start($start)
try {
    if (!$process.WaitForExit($TimeoutSeconds * 1000)) { $process.Kill(); $process.WaitForExit(); throw "Projectile check timed out; inspect $log" }
    if ($process.ExitCode -ne 0) { throw "Projectile check exited $($process.ExitCode); inspect $log" }
} finally { $process.Dispose() }
function Snapshot([string]$Name) {
    $zip = [IO.Compression.ZipFile]::OpenRead((Join-Path $runRoot "projectile-$Name.dsave"))
    try {
        $reader = [IO.StreamReader]::new($zip.GetEntry('session.json').Open())
        try { $data = $reader.ReadToEnd() | ConvertFrom-Json -AsHashtable } finally { $reader.Dispose() }
    } finally { $zip.Dispose() }
    $player = @($data.objects | Where-Object { $_.classtype -eq 'BloodPlayer' -and $_.Contains('actor') -and $null -ne $_.actor })[0]
    $projectiles = @($data.objects | Where-Object { $_['superset.foreign_projectile'] })
    $trails = @($data.objects | Where-Object { $_['superset.foreign_fx'] -and $_.classtype -eq 'DukeFirelaserTrail' })
    $target = @($data.objects | Where-Object { $_.classtype -eq 'BloodActor' -and $_.sprite.owner -eq 31034 })[0]
    foreach ($actor in @($projectiles) + @($trails)) {
        if (($actor.sprite.cstat -band 257) -or $actor.sprite.picnum -le 32767 -or !$actor.Contains('superset.attack_source')) {
            throw "Invalid foreign collider/art/source in $Name; inspect $log"
        }
    }
    return @{ Data=$data; Player=$player; Body=$data.objects[$player.actor]; Projectiles=$projectiles; Trails=$trails; Target=$target }
}
$s = @{}
foreach ($name in @('flight','held','restored','hit','quiet','offscreen','killed','thingbefore','thingafter','playerbefore','playerafter','floor','wall','cleared')) {
    $s[$name] = Snapshot $name
}
foreach ($name in @('flight','held','restored')) {
    if ($s[$name].Projectiles.Count -ne 1 -or $s[$name].Projectiles[0].classtype -ne 'DukeFirelaser' -or
        $s[$name].Projectiles[0]['superset.attack_source'] -ne $s[$name].Player.actor -or $s[$name].Target.xsprite.health -ne 16000) {
        throw "Flight/source/held target failed in $name; inspect $log"
    }
}
foreach ($name in @('held','restored')) {
    $before = $s.flight; $after = $s[$name]
    $steps = ($after.Data.state.frameclock - $before.Data.state.frameclock) / 4
    $dx = $after.Projectiles[0].sprite.pos[0] - $before.Projectiles[0].sprite.pos[0]
    $dy = $after.Projectiles[0].sprite.pos[1] - $before.Projectiles[0].sprite.pos[1]
    $dz = $after.Projectiles[0].sprite.pos[2] - $before.Projectiles[0].sprite.pos[2]
    $angle = if ($before.Projectiles[0].sprite.Contains('angles')) { $before.Projectiles[0].sprite.angles[1] * [Math]::PI / 180 } else { 0 }
    $speed = $before.Projectiles[0].vel[0]
    # Native clipmove quantizes each component to Build's 1/16-unit XY grid.
    $stepX = [Math]::Floor([Math]::Cos($angle) * $speed * 8) / 16
    $stepY = [Math]::Floor([Math]::Sin($angle) * $speed * 8) / 16
    if ($steps -le 0 -or [Math]::Abs($dx - $stepX*$steps) -gt 0.001 -or [Math]::Abs($dy - $stepY*$steps) -gt 0.001 -or
        [Math]::Abs($dz - $before.Projectiles[0].vel[2] * .5 * $steps) -gt 0.0001) {
        throw "Native half-step movement/clock failed in $name; inspect $log"
    }
}
if ($s.hit.Projectiles.Count -or $s.hit.Target.xsprite.health -ge 16000 -or
    $s.hit.Target.xsprite.burntime -gt 0 -or $s.hit.Target.xsprite.target -ne $s.hit.Player.actor -or
    $s.quiet.Target.xsprite.health -ne $s.hit.Target.xsprite.health -or $s.quiet.Projectiles.Count -or $s.quiet.Trails.Count) {
    throw "Native contact/one hit/retaliation/independent ignition or trail expiry failed; inspect $log"
}
if ($s.killed.Projectiles.Count -or $s.killed.Data.statistics.levels[-1].killcount -ne 1 -or
    $s.killed.Data.engine.maplocals.kills.player[0] -ne 1 -or
    ($s.killed.Target -and $s.killed.Target.xsprite.health -gt 0)) { throw "Native death/kill credit failed; inspect $log" }
if ($s.thingafter.Projectiles.Count -or $s.thingafter.Target.xsprite.health -ge $s.thingbefore.Target.xsprite.health) {
    throw "Native Blood thing was not damaged; inspect $log"
}
if ($s.playerafter.Projectiles.Count -or $s.playerafter.Body.xsprite.health -ge $s.playerbefore.Body.xsprite.health -or
    $s.playerafter.Player.armor[0] -ge 3200 -or $s.playerafter.Player.armor[0] -le 0) { throw "Player collision/armor failed; inspect $log" }
foreach ($name in @('floor','wall','cleared')) {
    if ($s[$name].Projectiles.Count) { throw "Projectile survived $name; inspect $log" }
}
if ($s.offscreen.Projectiles.Count -or $s.offscreen.Trails.Count) { throw "Unseen projectile/trail lifetime leaked; inspect $log" }
if ($s.cleared.Trails.Count -or $s.cleared.Target -or (Select-String -LiteralPath $log -Pattern 'Superset .*FAIL') -or
    !(Select-String -LiteralPath $log -SimpleMatch 'Superset shared Blood health check: Duke hit/VM=PASS') -or
    !(Select-String -LiteralPath $log -SimpleMatch 'Superset Duke projectile death commit: PASS.') -or
    !(Select-String -LiteralPath $log -SimpleMatch 'Superset Duke projectile environment: links=PASS, water roles=PASS, slowdown=PASS, Blood wall events=PASS.')) {
    throw "Cleanup/shared health regression failed; inspect $log"
}
Write-Output 'Duke projectiles: PASS (native Tick/movement, Blood enemies/things/player armor, contacts, source, save/GC and cleanup)'
Write-Output "Artifacts: $runRoot"
