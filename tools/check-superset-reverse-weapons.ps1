#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$DukeGrp,
    [Parameter(Mandatory)][string]$BloodRff,
    [string]$ExePath = (Join-Path $PSScriptRoot '../build-mashup/Release/raze.exe'),
    [ValidateRange(30, 180)][int]$TimeoutSeconds = 90,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../build-mashup/checks')
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'superset-check-common.ps1')
$ExePath = (Resolve-Path -LiteralPath $ExePath).Path
$DukeGrp = (Resolve-Path -LiteralPath $DukeGrp).Path
$BloodRff = (Resolve-Path -LiteralPath $BloodRff).Path
$runRoot = [IO.Path]::GetFullPath((Join-Path $OutputDirectory ('reverse-weapons-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfff'))))
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
$config = Join-Path $runRoot 'raze.ini'; $script = Join-Path $runRoot 'check.cfg'; $log = Join-Path $runRoot 'raze.log'
@'
[GlobalSettings]
vid_fullscreen=false
vid_preferbackend=1
vid_activeinbackground=true
i_pauseinbackground=false
vid_vsync=false
cl_nostartscreens=true
use_mouse=false
use_joystick=false
'@ | Set-Content -LiteralPath $config
$commands = @('wait 100', 'superset_duke_projectile_check arena', 'wait 8', 'superset_duke_weapon_check target', 'wait 4', 'superset_duke_weapon_check',
    'save reverse-start "Native Duke pistol on Caleb in Blood"', 'wait 4', 'screenshot',
    '+Fire', 'wait 30', '-Fire', 'wait 30', 'superset_duke_weapon_check',
    'save reverse-fired "Native Duke pistol firing"', 'wait 4', 'superset_duke_weapon_check give', 'slot 3', 'wait 60', 'superset_duke_weapon_check target', 'wait 4',
    'save reverse-shotgun-ready "Native Duke shotgun ready"', 'wait 4', '+Fire', 'wait 30', '-Fire', 'wait 40',
    'save reverse-shotgun-fired "Native Duke shotgun firing"', 'wait 4', 'slot 4', 'wait 60', 'superset_duke_weapon_check target', 'wait 4',
    'save reverse-chain-ready "Native Duke chaingun ready"', 'wait 4', '+Fire', 'wait 30', '-Fire', 'wait 20',
    'save reverse-chain-fired "Native Duke chaingun firing"', 'wait 4', 'superset_weapons blood', 'wait 80',
    'save reverse-blood "Blood weapons returned"', 'wait 4', 'superset_weapons duke', 'wait 80',
    'save reverse-duke "Duke inventory retained"', 'wait 4', 'map E1M2', 'wait 60', 'load reverse-chain-fired', 'wait 12',
    'gc full', 'superset_duke_weapon_check', 'save reverse-restored "Foreign weapon state restored after fresh map"', 'wait 4', 'screenshot',
    'superset_duke_weapon_check clear', 'slot 1', 'wait 60', 'superset_duke_weapon_check target', 'wait 4',
    '+Move_Forward', 'wait 8', '-Move_Forward', 'wait 4', 'save reverse-knee-ready "Native Duke knee ready"',
    '+Fire', 'wait 30', '-Fire', 'wait 20', 'save reverse-knee-fired "Native Duke knee in Blood world"', 'quit')
Write-SupersetCheckScript -Commands $commands -ScriptPath $script
$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = $ExePath; $start.WorkingDirectory = Split-Path -Parent $ExePath
$start.UseShellExecute = $false; $start.CreateNoWindow = $true; $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
foreach ($arg in @('-iwad', $BloodRff, '-superset', '-dukedata', $DukeGrp, '-superset-player', 'caleb', '-superset-weapons', 'duke',
    '-noautoload', '-noautoexec', '-nologo', '-nomonsters', '-nomusic', '-config', $config, '-savedir', $runRoot, '-shotdir', $runRoot,
    '-map', 'E1M1', '+logfile', $log, '+iso_debug', '0', '+exec', $script)) { $start.ArgumentList.Add($arg) }
$process = [Diagnostics.Process]::Start($start)
try {
    if (!$process.WaitForExit($TimeoutSeconds * 1000)) { $process.Kill(); $process.WaitForExit(); throw "Reverse weapon check timed out; inspect $log" }
    if ($process.ExitCode -ne 0) { throw "Reverse weapon check exited $($process.ExitCode); inspect $log" }
} finally { $process.Dispose() }
function Read-State([string]$Name) {
    $zip = [IO.Compression.ZipFile]::OpenRead((Join-Path $runRoot "reverse-$Name.dsave"))
    try {
        $reader = [IO.StreamReader]::new($zip.GetEntry('session.json').Open())
        try { $data = $reader.ReadToEnd() | ConvertFrom-Json -AsHashtable } finally { $reader.Dispose() }
    } finally { $zip.Dispose() }
    $p = $data.objects[$data['superset.duke_program'].players[0]]
    $target = @($data.objects | Where-Object { $_.classtype -eq 'BloodActor' -and $_.sprite.owner -eq 31036 })[0]
    return @{ Data = $data; Duke = $p; Body = $data.objects[$p.actor]; Target = $target }
}
$states = @{}
foreach ($name in @('start', 'fired', 'shotgun-ready', 'shotgun-fired', 'chain-ready', 'chain-fired', 'blood', 'duke', 'restored', 'knee-ready', 'knee-fired')) {
    $s = Read-State $name; $states[$name] = $s
    if ($s.Body.classtype -ne 'DukePlayerBase' -or ($s.Body.sprite.cstat -band 257) -or
        @($s.Data.objects | Where-Object { $_.classtype -eq 'BloodPlayer' -and $_.Contains('actor') }).Count -ne 1) {
        throw "Invalid foreign weapon body or native player in $name; inspect $log"
    }
}
foreach ($pair in @(@('start', 'fired', 1), @('shotgun-ready', 'shotgun-fired', 2), @('chain-ready', 'chain-fired', 3))) {
    $before = $states[$pair[0]].Duke; $after = $states[$pair[1]].Duke; $weapon = $pair[2]
    if ($before.curr_weapon -ne $weapon -or $after.curr_weapon -ne $weapon -or $after.ammo_count[$weapon] -ge $before.ammo_count[$weapon]) {
        throw "Native weapon $weapon did not consume ammo; inspect $log"
    }
    $initial = $states[$pair[0]].Target; $hit = $states[$pair[1]].Target
    if (!$initial -or !$hit -or $hit.xsprite.health -ge $initial.xsprite.health -or $hit.xsprite.health -le 0) {
        throw "Native weapon $weapon did not damage the Blood target; inspect $log"
    }
    # XSPRITE serializes its bitfields as flags; isTriggered is bit 7.
    if (($initial.xsprite.flags -band 128) -or !($hit.xsprite.flags -band 128)) {
        throw "Native weapon $weapon did not dispatch Blood's vector trigger; inspect $log"
    }
}
if ($states['knee-ready'].Duke.curr_weapon -ne 0 -or !$states['knee-fired'].Target -or
    $states['knee-fired'].Target.xsprite.health -ge $states['knee-ready'].Target.xsprite.health) {
    throw "Native melee did not damage the Blood target; inspect $log"
}
if ($states.blood.Duke['superset.weapon_mode'] -ne 1 -or $states.duke.Duke['superset.weapon_mode'] -ne 0 -or
    $states.restored.Duke.curr_weapon -ne 3 -or $states.restored.Duke.ammo_count[3] -ne $states['chain-fired'].Duke.ammo_count[3]) {
    throw "Weapon handoff/inventory restoration failed; inspect $log"
}
$text = Get-Content -LiteralPath $log -Raw
if ($text -match '\bFAIL\b|VM execution aborted') { throw "Reverse weapon runtime check failed; inspect $log" }
Write-Output 'Reverse weapons: PASS (native pistol/shotgun/chaingun/knee damage to Blood enemy, native input and ammo, Blood player identity, one collider, family handoff, saved native inventory and GC)'
Write-Output "Artifacts: $runRoot"
