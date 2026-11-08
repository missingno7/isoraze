#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$DukeGrp,
    [Parameter(Mandatory)][string]$BloodRff,
    [string]$ExePath = (Join-Path $PSScriptRoot '../build-mashup-dev/Release/raze.exe'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../build-mashup-dev/checks')
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'superset-check-common.ps1')
$ExePath = (Resolve-Path -LiteralPath $ExePath).Path
$DukeGrp = (Resolve-Path -LiteralPath $DukeGrp).Path
$BloodRff = (Resolve-Path -LiteralPath $BloodRff).Path
$root = [IO.Path]::GetFullPath((Join-Path $OutputDirectory ('effects-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfff'))))
foreach ($profile in @('E1L1-caleb','E1L1-duke','E3M1-caleb')) {
    $map,$character = $profile.Split('-')
    $caseRoot = Join-Path $root $profile
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
    $commands = @('wait 100','god','superset_projectile_check isolate','wait 4','superset_duke_projectile_check arena',
        'wait 8','superset_duke_projectile_check faceclear','superset_effect_check equip','wait 12',
        'superset_pickup_switch 0','superset_weapon_bank 3 blood.shotgun','superset_weapon_bank 4 blood.tommygun',
        'slot 3','wait 100','superset_effect_check clear','+Fire','wait 100','-Fire','wait 180',
        'superset_effect_check','save shotgun','slot 4','wait 100','superset_effect_check clear',
        '+Fire','wait 40','-Fire','wait 180','superset_effect_check','save tommy',
        'superset_effect_check flare','wait 8','superset_effect_check','save mixed',
        'load mixed','wait 8','gc full','superset_effect_check','save restored','quit')
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
        if (!$process.WaitForExit(180000)) { $process.Kill(); $process.WaitForExit(); throw "$profile timed out; inspect $log" }
        if ($process.ExitCode -ne 0) { throw "$profile exited $($process.ExitCode); inspect $log" }
    } finally { $process.Dispose() }
    $output = Get-Content -LiteralPath $log -Raw
    if ($output -match '\bFAIL\b|VM execution aborted') { throw "Runtime failure; inspect $log" }
    $samples = [regex]::Matches($output, 'Superset effects: PASS \(effects=(\d+) settled=(\d+) marks=(\d+) flares=(\d+) halos=(\d+) glows=(\d+) coexist=1\)')
    if ($samples.Count -ne 4) { throw "$profile`: missing effect checks; inspect $log" }
    foreach ($sample in $samples[0..1]) {
        if ([int]$sample.Groups[2].Value -le 0) { throw "$profile`: no settled casings; inspect $log" }
        if ($map -eq 'E1L1' -and [int]$sample.Groups[3].Value -le 0) { throw "$profile`: no wall marks; inspect $log" }
    }
    foreach ($sample in $samples[2..3]) {
        if ([int]$sample.Groups[4].Value -le 0 -or [int]$sample.Groups[5].Value -le 0 -or [int]$sample.Groups[6].Value -le 0) {
            throw "$profile`: missing flare halo/glow before or after load; inspect $log"
        }
    }
    Write-Output "$profile`: PASS (native firing, wall marks, settled casings, flare presentation, source-qualified textures, mixed draw pass and save/load/GC)"
}
Write-Output "Artifacts: $root"
