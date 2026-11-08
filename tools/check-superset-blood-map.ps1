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
$runRoot = [IO.Path]::GetFullPath((Join-Path $OutputDirectory ('blood-map-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfff'))))
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
$commands = @('wait 90', 'superset_check', 'superset_duke_resource_check', 'mashup_runtime_check',
    'superset_duke_effect_check native', 'superset_spawn_duke_effect splat', 'wait 8', 'superset_duke_effect_check',
    'superset_fx_check seed', 'save bloodmap-start "Native Blood and Duke actors"', 'wait 4', 'screenshot',
    'wait 20', 'superset_duke_effect_check', 'save bloodmap-held "Original Duke tick alongside native Blood"', 'wait 4',
    'superset_duke_effect_check clear', 'superset_fx_check clear', 'map E1M2', 'wait 60', 'superset_check', 'superset_duke_resource_check',
    'load bloodmap-start', 'wait 4', 'gc full', 'superset_duke_effect_check', 'superset_fx_check saved',
    'save bloodmap-restored "Foreign actor and native events restored after fresh map"', 'wait 4', 'screenshot',
    'wait 400', 'superset_duke_effect_check', 'superset_fx_check expired', 'save bloodmap-aged "Native decal growth stopped at original limit"', 'wait 4',
    'give weapons', 'slot 2', 'wait 60', 'superset_duke_effect_check target', 'save bloodmap-flarebefore "Native Blood flare ready"', 'wait 30',
    '+Fire', 'wait 30', '-Fire', 'wait 4', 'save bloodmap-flareafter "Native Blood flare produced missiles"', 'wait 4',
    'superset_duke_effect_check targetresume', 'wait 8', 'save bloodmap-burndamage "Native health damage from burning ticks"', 'wait 4',
    'superset_duke_effect_check cleartarget', 'superset_duke_effect_check facewall', 'wait 4')
foreach ($i in 1..65) { $commands += 'superset_spawn_duke_effect splat' }
$commands += @('wait 8', 'superset_duke_effect_check', 'gc full', 'save bloodmap-queued "Native Duke queue capacity and eviction"', 'wait 4',
    'map E1M2', 'wait 60', 'load bloodmap-queued', 'wait 4', 'gc full', 'superset_duke_effect_check',
    'save bloodmap-queue-restored "Restored native queue"', 'wait 4', 'superset_duke_effect_check clear', 'gc full',
    'save bloodmap-cleared "Foreign actors removed without native world damage"', 'wait 4', 'mashup_runtime_check', 'quit')
Write-SupersetCheckScript -Commands $commands -ScriptPath $script
$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = $ExePath
$start.WorkingDirectory = Split-Path -Parent $ExePath
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
foreach ($arg in @('-iwad', $BloodRff, '-superset', '-dukedata', $DukeGrp, '-superset-player', 'caleb', '-superset-weapons', 'blood',
    '-noautoload', '-noautoexec', '-nologo', '-nomonsters', '-nomusic', '-config', $config, '-savedir', $runRoot, '-shotdir', $runRoot,
    '-map', 'E1M1', '+logfile', $log, '+iso_debug', '0', '+exec', $script)) { $start.ArgumentList.Add($arg) }
if (!$EnableSound) { $start.ArgumentList.Add('-nosound') }
$process = [Diagnostics.Process]::Start($start)
try {
    if (!$process.WaitForExit($TimeoutSeconds * 1000)) { $process.Kill(); $process.WaitForExit(); throw "Blood map check timed out; inspect $log" }
    if ($process.ExitCode -ne 0) { throw "Blood map check exited $($process.ExitCode); inspect $log" }
} finally { $process.Dispose() }
function Read-State([string]$Name) {
    $zip = [IO.Compression.ZipFile]::OpenRead((Join-Path $runRoot "bloodmap-$Name.dsave"))
    try {
        $reader = [IO.StreamReader]::new($zip.GetEntry('session.json').Open())
        try { $data = $reader.ReadToEnd() | ConvertFrom-Json -AsHashtable } finally { $reader.Dispose() }
        $reader = [IO.StreamReader]::new($zip.GetEntry('info.json').Open())
        try { $info = $reader.ReadToEnd() | ConvertFrom-Json -AsHashtable } finally { $reader.Dispose() }
    } finally { $zip.Dispose() }
    $effects = @($data.objects | Where-Object { $_['superset.foreign_fx'] })
    $player = @($data.objects | Where-Object { $_.classtype -eq 'BloodPlayer' -and $_.Contains('actor') -and $null -ne $_.actor })[0]
    return @{ Data = $data; Info = $info; Effects = $effects; Player = $player }
}
$states = @{}
foreach ($name in @('start', 'held', 'flarebefore', 'flareafter', 'burndamage', 'restored', 'aged', 'queued', 'queue-restored', 'cleared')) {
    $s = Read-State $name
    $states[$name] = $s
    if (!$s.Player -or $s.Info.Engine -ne 'Raze.Superset' -or $s.Info.Superset.map_rules -ne 'blood' -or
        $s.Info.Superset.blood_archive_md5.Length -ne 32 -or $s.Info.Superset.duke_bank_md5.Length -ne 32 -or
        $s.Data['superset.gamestate'].player_controller -ne 1 -or $s.Data.Contains('superset.blood_controller')) {
        throw "Invalid reverse adapter identity/player/native state in $name; inspect $log"
    }
    foreach ($effect in $s.Effects) {
        if ($effect.classtype -ne 'DukeBloodSplat1' -or ($effect.sprite.cstat -band 257) -or $effect.sprite.picnum -le 32767 -or
            $effect.counter -le 0 -or $effect.counter -gt 364) { throw "Invalid foreign effect state in $name; inspect $log" }
    }
}
foreach ($name in @('start', 'held', 'flarebefore', 'flareafter', 'burndamage', 'restored', 'aged')) {
    if ($states[$name].Effects.Count -ne 1) { throw "Lost/duplicated Duke actor in $name; inspect $log" }
}
if ($states.held.Effects[0].counter -le $states.start.Effects[0].counter -or
    ($states.held.Effects[0].counter - $states.start.Effects[0].counter) -ne (($states.held.Data.state.frameclock - $states.start.Data.state.frameclock) / 4) -or
    ($states.held.Data.state.framecount - $states.start.Data.state.framecount) -ne ($states.held.Effects[0].counter - $states.start.Effects[0].counter) -or
    $states.held.Effects[0].sprite.pos[2] -le $states.start.Effects[0].sprite.pos[2] -or
    $states.restored.Effects[0].counter -ne ($states.start.Effects[0].counter + ($states.restored.Data.state.frameclock - $states.start.Data.state.frameclock) / 4) -or
    $states.restored.Effects[0].counter -ge $states.held.Effects[0].counter -or $states.aged.Effects[0].counter -ne 364) {
    throw "Native tick/growth limit or saved counter restoration failed; inspect $log"
}
$targetBefore = @($states.flarebefore.Data.objects | Where-Object { $_.classtype -eq 'BloodActor' -and $_.sprite.owner -eq 31032 })[0]
$targetAfter = @($states.flareafter.Data.objects | Where-Object { $_.classtype -eq 'BloodActor' -and $_.sprite.owner -eq 31032 })[0]
if ($states.flareafter.Player.ammocount[1] -ge $states.flarebefore.Player.ammocount[1] -or !$targetBefore -or !$targetAfter -or
    $targetAfter.xsprite.burntime -le 0 -or
    $targetAfter.xsprite.burnsource -ne $states.flareafter.Player.actor) {
    throw "Native Blood flare did not ignite its target with correct ownership; inspect $log"
}
$burned = @($states.burndamage.Data.objects | Where-Object { $_.classtype -eq 'BloodActor' -and $_.sprite.owner -eq 31032 })[0]
if (!$burned -or $burned.xsprite.health -ge $targetAfter.xsprite.health -or $burned.xsprite.burntime -ge $targetAfter.xsprite.burntime) {
    throw "Native burning did not resume health/time processing; inspect $log"
}
foreach ($name in @('queued', 'queue-restored')) {
    $s = $states[$name]
    if ($s.Effects.Count -ne 64 -or $s.Data['superset.duke_effects'].capacity -ne 64 -or
        @($s.Data['superset.duke_effects'].queue | Where-Object { $null -ne $_ }).Count -ne 64) {
        throw "Native queue cap/eviction or restoration failed in $name; inspect $log"
    }
}
if ($states.cleared.Effects.Count) { throw "Foreign actor cleanup failed; inspect $log" }
$expected = @('Superset check: identity/config=PASS, content manifest=PASS, save isolation=PASS, shared units=PASS.',
    'Superset Duke resource check: isolation/colors/aliases/private files=PASS.',
    'Superset native Blood thing check: PASS (TNT barrel health/hitscan blocking).', 'Superset FX saved check: PASS.', 'Superset FX expired check: PASS.')
foreach ($line in $expected) {
    if (!(Select-String -LiteralPath $log -SimpleMatch $line)) { throw "Missing check '$line'; inspect $log" }
}
if (!(Select-String -LiteralPath $log -SimpleMatch 'Superset native Blood player check: PASS') -or
    (Select-String -LiteralPath $log -SimpleMatch 'Superset Duke effect check: FAIL') -or
    @(Select-String -LiteralPath $log -SimpleMatch 'Mashup runtime check: Duke=PASS, Blood=PASS, core=PASS, sector state=PASS, sector save=PASS, removal=PASS, cleanup=PASS.').Count -ne 2 -or
    !(Select-String -LiteralPath $config -SimpleMatch '[Superset.')) { throw "Runtime isolation/configuration checks failed; inspect $log" }
Write-Output 'Blood map: PASS (native Blood player, missiles, object blocking/events; native Duke actor ticks/art, queue, save/GC and cleanup)'
Write-Output "Artifacts: $runRoot"
