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
$root = [IO.Path]::GetFullPath((Join-Path $OutputDirectory ('arsenal-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfff'))))
function Read-State([string]$Path) {
    $zip = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $reader = [IO.StreamReader]::new($zip.GetEntry('session.json').Open())
        try { $data = $reader.ReadToEnd() | ConvertFrom-Json -AsHashtable } finally { $reader.Dispose() }
    } finally { $zip.Dispose() }
    $views = @($data.objects | Where-Object { $_.Contains('superset.inventory') -and $_['superset.inventory'] -is [long] })
    $ids = @($views | ForEach-Object { $_['superset.inventory'] } | Sort-Object -Unique)
    if ($ids.Count -ne 1 -or $views.Count -lt 2) { throw "Multiple inventory identities in $Path" }
    $inventory = $data.objects[$ids[0]]
    if ($inventory.classtype -ne 'GameplayInventory') { throw "Wrong inventory class in $Path" }
    foreach ($view in $views) {
        if ($view.Contains('ammocount') -or $view.Contains('ammo_count') -or $view.Contains('hasweapon') -or $view.Contains('gotweapon')) {
            throw "Native view serialized a second inventory in $Path"
        }
    }
    return $inventory
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
    $commands = @('wait 100', 'god')
    if ($format -eq 'blood') { $commands += 'superset_duke_projectile_check arena' }
    $commands += @('save pickup-empty', 'superset_pickup_switch 0',
        'superset_spawn blood.shotgun here', 'superset_level_check collect', 'wait 60', 'save pickup-manual-blood',
        'superset_spawn duke.shotgun here', 'superset_level_check collect', 'wait 60', 'save pickup-manual-duke',
        'superset_pickup_switch 1', 'superset_spawn blood.shotgun here', 'superset_level_check collect', 'wait 60', 'save pickup-owned-blood',
        'superset_spawn duke.shotgun here', 'superset_level_check collect', 'wait 60', 'save pickup-owned-duke',
        'load pickup-empty', 'wait 3', 'superset_pickup_switch 1',
        'superset_spawn blood.shotgun here', 'superset_level_check collect', 'wait 60', 'save pickup-new-blood',
        'superset_spawn duke.shotgun here', 'superset_level_check collect', 'wait 60', 'save pickup-new-duke',
        'superset_pickup_switch 2', 'superset_spawn blood.shotgun here', 'superset_level_check collect', 'wait 60', 'save pickup-always-blood',
        'superset_spawn duke.shotgun here', 'superset_level_check collect', 'wait 60', 'save pickup-always-duke',
        'load pickup-empty', 'wait 3', 'superset_pickup_switch 0', 'superset_arsenal_check give', 'slot 3', 'wait 100', 'superset_arsenal_check',
        'save arsenal-duke "Bank 3 native Duke shotgun"', '+Fire', 'wait 30', '-Fire', 'wait 60',
        'save arsenal-duke-fired "Native Duke shotgun consumed shared Duke shells"', 'slot 3', 'wait 100',
        'save arsenal-blood "Bank 3 native Blood shotgun"', '+Alt_Fire', 'wait 6', 'slot 3', 'wait 1',
        'save arsenal-pending "Native Blood alternate attack before pending Duke selection"', '-Alt_Fire', 'wait 100',
        'save arsenal-returned "Blood alternate completed before switching"', 'load arsenal-pending', 'wait 100',
        'gc full', 'superset_arsenal_check', 'save arsenal-restored "Restore pending native QAV and bank selection"',
        'slot 3', 'wait 100', 'save arsenal-bloodagain "Both shotguns remain owned"',
        'weapnext', 'wait 80', 'save arsenal-next "Next shared weapon"',
        'weapprev', 'wait 80', 'save arsenal-prev "Previous shared weapon"',
        'superset_weapon_bank 3 blood.shotgun duke.shotgun', 'save arsenal-order "Configured bank order"',
        'wait 2', ('map ' + $(if ($format -eq 'blood') { 'E1M2' } else { 'E1L2' })), 'wait 60',
        'load arsenal-order', 'wait 12', 'superset_arsenal_check', 'save arsenal-order-restored "Bank order persists"', 'quit')
    Write-SupersetCheckScript -Commands $commands -ScriptPath $script
    $worldArgs = if ($format -eq 'blood') { @('-iwad',$BloodRff,'-superset','-dukedata',$DukeGrp,'-map','E1M1') }
        else { @('-iwad',$DukeGrp,'-superset','-blooddata',(Split-Path -Parent $BloodRff),'-map','E1L1') }
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $ExePath; $start.WorkingDirectory = Split-Path -Parent $ExePath
    $start.UseShellExecute = $false; $start.CreateNoWindow = $true; $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    foreach ($arg in ($worldArgs + @('-superset-player','caleb','-superset-weapons','blood',
        '-noautoload','-noautoexec','-nomonsters','-nomusic','-config',$config,'-savedir',$caseRoot,
        '+iso_debug','0','+logfile',$log,'+exec',$script))) { $start.ArgumentList.Add($arg) }
    $process = [Diagnostics.Process]::Start($start)
    try {
        if (!$process.WaitForExit($TimeoutSeconds * 1000)) { $process.Kill(); $process.WaitForExit(); throw "$format timed out; inspect $log" }
        if ($process.ExitCode -ne 0) { throw "$format exited $($process.ExitCode); inspect $log" }
    } finally { $process.Dispose() }
    if ((Get-Content -LiteralPath $log -Raw) -match '\bFAIL\b|VM execution aborted') { throw "Runtime failure; inspect $log" }
    foreach ($name in @('manual-blood','manual-duke','owned-blood','owned-duke','new-blood','new-duke','always-blood','always-duke')) {
        $inventory = Read-State (Join-Path $caseRoot "pickup-$name.dsave")
        $expected = if ($name -like 'manual-*' -or $name -like 'owned-*') { 33 } elseif ($name -like '*-blood') { 35 } else { 3 }
        if ($inventory.selected_weapon -ne $expected -or $inventory.pending_weapon) { throw "$format common pickup-switch policy failed in $name" }
        if ($name -like '*-blood' -and !$inventory.blood_weapons[3] -or $name -like '*-duke' -and !$inventory.duke_weapons[2]) {
            throw "$format pickup-switch fixture was not collected in $name"
        }
    }
    $states = @{}
    foreach ($name in @('duke','duke-fired','blood','pending','returned','restored','bloodagain','next','prev','order','order-restored')) {
        $states[$name] = Read-State (Join-Path $caseRoot "arsenal-$name.dsave")
        if (!$states[$name].duke_weapons[2] -or !$states[$name].blood_weapons[3]) { throw "$format lost shotgun ownership in $name" }
    }
    if ($states.duke.selected_weapon -ne 3 -or $states.blood.selected_weapon -ne 35 -or
        $states['duke-fired'].duke_ammo[2] -ge $states.duke.duke_ammo[2] -or
        $states['duke-fired'].blood_ammo[2] -ne 20) { throw "$format Duke selection/firing isolation failed" }
    if ($states.pending.selected_weapon -ne 35 -or $states.pending.pending_weapon -ne 3 -or
        $states.returned.selected_weapon -ne 3 -or $states.restored.selected_weapon -ne 3 -or
        $states.returned.blood_ammo[2] -ne 18 -or $states.restored.blood_ammo[2] -ne 18 -or
        $states.returned.duke_ammo[2] -ne $states.blood.duke_ammo[2]) { throw "$format native alternate/pending selection restore failed" }
    if ($states.bloodagain.selected_weapon -ne 35 -or $states.next.selected_weapon -eq 35 -or $states.prev.selected_weapon -ne 35 -or
        ($states['order-restored'].bank_3 -join ',') -ne '35,3') { throw "$format bank cycling/order persistence failed" }
    Write-Output "$format arsenal: PASS (one inventory, pickup-switch policies 0/1/2 for both games, bank 3, native fire/alternate, pending switch, next/previous, saved order)"
}
Write-Output "Artifacts: $root"
