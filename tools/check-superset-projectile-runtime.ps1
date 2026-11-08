#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$DukeGrp,
    [Parameter(Mandatory)][string]$BloodRff,
    [string]$ExePath = (Join-Path $PSScriptRoot '../build-mashup-dev/Release/raze.exe'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../build-mashup-dev/checks'),
    [ValidateRange(30,240)][int]$TimeoutSeconds = 180
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'superset-check-common.ps1')
$ExePath = (Resolve-Path -LiteralPath $ExePath).Path
$DukeGrp = (Resolve-Path -LiteralPath $DukeGrp).Path
$BloodRff = (Resolve-Path -LiteralPath $BloodRff).Path
$root = [IO.Path]::GetFullPath((Join-Path $OutputDirectory ('projectile-runtime-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfff'))))
function Read-State([string]$Path) {
    $zip = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $reader = [IO.StreamReader]::new($zip.GetEntry('session.json').Open())
        try { return $reader.ReadToEnd() | ConvertFrom-Json -AsHashtable } finally { $reader.Dispose() }
    } finally { $zip.Dispose() }
}
function Get-Tagged($Data, [int]$Tag) {
    return @($Data.objects | Where-Object { $_.sprite.owner -eq $Tag })[0]
}
function Get-Clock($Data) {
    if ($Data.ContainsKey('state')) { return [int]$Data.state.frameclock }
    return [int]$Data['duke.gamestate'].levelclock
}
function Get-Health($Actor) {
    if ($Actor.classtype -eq 'BloodActor') { return [int]$Actor.xsprite.health }
    return [int]$Actor.sprite.extra * 16 - [int]$Actor['superset.damage_remainder']
}
function Get-FlareAmmo($Data) {
    $inventory = @($Data.objects | Where-Object classtype -eq 'GameplayInventory')[0]
    return [int]$inventory.blood_ammo[1]
}
function Get-Attached($Data) {
    return @($Data.objects | Where-Object { $_.classtype -eq 'BloodActor' -and $_.sprite.statnum -eq 14 -and $_.xsprite.target -is [long] })
}
function Assert-Attachment($Data, [string]$Label) {
    $target = Get-Tagged $Data 31041
    $attachments = @(Get-Attached $Data)
    if (!$target -or !$attachments.Count) { throw "${Label}: missing target/attached flare" }
    foreach ($flare in $attachments) {
        $receiver = $Data.objects[$flare.xsprite.target]
        if ($receiver.sprite.owner -ne 31041) { throw "${Label}: flare targets another actor" }
        $expectedZ = [double]$receiver.sprite.pos[2] + [double]$flare.xsprite.targetZ
        if ([Math]::Abs([double]$flare.sprite.pos[2] - $expectedZ) -gt 1) { throw "${Label}: attachment did not follow target Z" }
        $dx = [double]$flare.sprite.pos[0] - [double]$receiver.sprite.pos[0]
        $dy = [double]$flare.sprite.pos[1] - [double]$receiver.sprite.pos[1]
        if ([Math]::Abs([Math]::Sqrt($dx*$dx + $dy*$dy) - [double]$receiver.clipdist * .5) -gt 1) { throw "${Label}: attachment did not follow target XY" }
    }
}
foreach ($map in @('E3M1','E1L1')) {
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
    $commands = @('wait 100','god','superset_trace_blood_impacts 30','superset_projectile_check isolate','wait 4','superset_duke_projectile_check arena','wait 8',
        'superset_duke_projectile_check faceclear','superset_projectile_check equip','wait 8','superset_weapon_bank 2 blood.flaregun duke.pistol','slot 2','wait 100',
        'superset_projectile_check flight','save flight-before','wait 16','save flight-after',
        'load flight-before','wait 16','gc full','save flight-restored','superset_projectile_check clear','wait 4','save ready')
    foreach ($kind in @('blood','duke')) {
        $commands += @('load ready','wait 4',"superset_projectile_check target $kind",'wait 4',"save $kind-before",
            '+Fire','wait 12','-Fire','wait 8',"save $kind-hit",'wait 12',"save $kind-follow",
            "load $kind-hit",'wait 12','gc full',"save $kind-restored",'superset_projectile_check kill','wait 12',"save $kind-dead",
            "load $kind-hit",'wait 4','superset_projectile_check destroy','wait 4','gc full',"save $kind-removed",
            'load ready','wait 4',"superset_projectile_check target $kind",'wait 4',"save $kind-alt-before",
            '+Alt_Fire','wait 16','-Alt_Fire','wait 20',"save $kind-alt-hit")
    }
    $commands += @('load ready','wait 4','superset_projectile_check flight','wait 180','gc full','save expired','quit')
    Write-SupersetCheckScript -Commands $commands -ScriptPath $script
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $ExePath; $start.WorkingDirectory = Split-Path -Parent $ExePath
    $start.UseShellExecute = $false; $start.CreateNoWindow = $true; $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $worldArgs = if ($map -eq 'E1L1') { @('-iwad',$DukeGrp,'-superset','-blooddata',(Split-Path -Parent $BloodRff),'-map',$map) }
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
    $states = @{}
    foreach ($name in @('ready','flight-before','flight-after','flight-restored','expired')) { $states[$name] = Read-State (Join-Path $caseRoot "$name.dsave") }
    $inventory = @($states.ready.objects | Where-Object classtype -eq 'GameplayInventory')[0]
    if (!$inventory.blood_weapons[2] -or $inventory.selected_weapon -ne 34 -or (Get-FlareAmmo $states.ready) -lt 8) { throw "$map`: native flare pickups/selection failed" }
    if ((Get-Content -LiteralPath $log -Raw) -notmatch 'Picked up Flare') { throw "$map`: missing native pickup evidence" }
    foreach ($name in @('flight-after','flight-restored')) {
        $before = Get-Tagged $states['flight-before'] 31042; $after = Get-Tagged $states[$name] 31042
        $steps = ((Get-Clock $states[$name]) - (Get-Clock $states['flight-before'])) / 4
        if (!$before -or !$after -or $steps -le 0) { throw "$map $name`: missing live flight or clock" }
        for ($axis = 0; $axis -lt 3; $axis++) {
            $expected = [double]$before.sprite.pos[$axis] + [double]$before.vel[$axis] * $steps
            if ([Math]::Abs([double]$after.sprite.pos[$axis] - $expected) -gt .1) { throw "$map $name`: projectile advanced incorrectly ($steps steps, axis $axis)" }
        }
    }
    if (Get-Tagged $states.expired 31042) { throw "$map`: offscreen lifetime callback did not remove flare" }
    foreach ($kind in @('blood','duke')) {
        foreach ($phase in @('before','hit','follow','restored','dead','removed','alt-before','alt-hit')) { $states[$phase] = Read-State (Join-Path $caseRoot "$kind-$phase.dsave") }
        $before = Get-Tagged $states.before 31041; $hit = Get-Tagged $states.hit 31041
        if ((Get-Health $hit) -ge (Get-Health $before) -or (Get-FlareAmmo $states.hit) -ge (Get-FlareAmmo $states.before)) { throw "$map $kind`: primary fire did not damage/spend ammo" }
        $burn = if ($kind -eq 'blood') { $hit.xsprite.burntime } else { $hit['superset.burn_time'] }
        if ($burn -le 0) { throw "$map $kind`: flare did not ignite target" }
        if ($kind -eq 'duke' -or @(Get-Attached $states.hit).Count) {
            foreach ($phase in @('hit','follow','restored')) { Assert-Attachment $states[$phase] "$map $kind $phase" }
        } else {
            $contacts = Get-Content -LiteralPath $log -Raw
            if ($contacts -notmatch 'missile=301 code=3 target=BloodActor[^\r\n]*surface=0' -or
                $contacts -match 'missile=301 code=3 target=BloodActor[^\r\n]*surface=4') { throw "$map Blood attachment missing despite flesh material or missing contact evidence" }
            Write-Output "$map Blood material: native non-flesh/unknown surface; attachment follows original material rule"
        }
        if (@(Get-Attached $states.dead).Count) { throw "$map $kind`: flares remained attached to dead target" }
        if (@($states.removed.objects | Where-Object { $_.classtype -eq 'BloodActor' -and $_.sprite.statnum -eq 14 }).Count) { throw "$map $kind`: orphaned flare survived target destruction/GC" }
        $before = Get-Tagged $states['alt-before'] 31041; $hit = Get-Tagged $states['alt-hit'] 31041
        if ((Get-Health $hit) -ge (Get-Health $before) -or (Get-FlareAmmo $states['alt-hit']) -gt (Get-FlareAmmo $states['alt-before']) - 8) { throw "$map $kind`: alternate fire did not damage/spend burst ammo" }
    }
    Write-Output "$map`: PASS (single projectile step, native primary/alternate, both enemy sources, ignition, attachment, death, save/load/GC, offscreen expiry)"
}
Write-Output "Artifacts: $root"
