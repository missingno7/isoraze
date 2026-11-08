#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$DukeGrp,
    [Parameter(Mandatory)][string]$BloodDirectory,
    [string]$ExePath = (Join-Path $PSScriptRoot '../build-mashup-dev/Release/raze.exe'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../build-mashup-dev/checks')
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'superset-check-common.ps1')
$ExePath = (Resolve-Path -LiteralPath $ExePath).Path
$DukeGrp = (Resolve-Path -LiteralPath $DukeGrp).Path
$BloodDirectory = (Resolve-Path -LiteralPath $BloodDirectory).Path
$root = [IO.Path]::GetFullPath((Join-Path $OutputDirectory ('air-transports-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfff'))))
function Read-State([string]$Path) {
    $zip = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $reader = [IO.StreamReader]::new($zip.GetEntry('session.json').Open())
        try { return $reader.ReadToEnd() | ConvertFrom-Json -AsHashtable } finally { $reader.Dispose() }
    } finally { $zip.Dispose() }
}
function Get-Body($Data) {
    $player = @($Data.objects | Where-Object { $_.classtype -eq 'DukePlayer' -and $_.pnum -eq 0 })[0]
    return $Data.objects[$player.actor]
}
foreach ($character in @('duke','caleb')) {
    $path = Join-Path $root $character
    New-Item -ItemType Directory -Path $path -Force | Out-Null
    $config = Join-Path $path 'raze.ini'; $script = Join-Path $path 'check.cfg'; $log = Join-Path $path 'raze.log'
    @'
[GlobalSettings]
vid_fullscreen=false
vid_activeinbackground=true
i_pauseinbackground=false
vid_vsync=false
cl_nostartscreens=true
cl_noexitscreens=true
cl_loadingscreens=false
use_mouse=false
use_joystick=false
'@ | Set-Content -LiteralPath $config
    # These coordinates refer to the unmodified Atomic E1L1 rooftop shaft.
    # -1710 reproduces a normal fall skipping the airborne sensor with Caleb.
    $commands = @('wait 90','god','save ready',
        'superset_map_contact_check drop 256 -1710','wait 100','save roof-outcome',
        'load ready','wait 4','superset_map_contact_check drop 256 -500 80','wait 20','save fast-outcome',
        'load ready','wait 4','superset_map_contact_check drop 256 -223.3125','wait 2','save slow-arrival',
        'wait 8','save slow-held')
    if ($character -eq 'caleb') {
        foreach ($step in 1..8) {
            $commands += @('load ready','wait 4','superset_map_contact_check drop 256 -500 80',"wait $step", "save checkpoint-$step",
                'wait 20',"save outcome-$step", "load checkpoint-$step",'wait 20','gc full',"save restored-$step")
        }
    }
    $commands += 'quit'
    Write-SupersetCheckScript -Commands $commands -ScriptPath $script
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $ExePath; $start.WorkingDirectory = Split-Path -Parent $ExePath
    $start.UseShellExecute = $false; $start.CreateNoWindow = $true; $start.WindowStyle = 'Hidden'
    foreach ($arg in @('-iwad',$DukeGrp,'-superset','-blooddata',$BloodDirectory,'-superset-player',$character,
        '-superset-weapons','blood','-map','E1L1','-noautoload','-noautoexec','-nomonsters','-nomusic',
        '-config',$config,'-savedir',$path,'+iso_debug','0','+logfile',$log,'+exec',$script)) { $start.ArgumentList.Add($arg) }
    $process = [Diagnostics.Process]::Start($start)
    try {
        if (!$process.WaitForExit(60000)) { $process.Kill(); $process.WaitForExit(); throw "$character timed out; inspect $log" }
        if ($process.ExitCode) { throw "$character exited $($process.ExitCode); inspect $log" }
    } finally { $process.Dispose() }
    foreach ($name in @('roof-outcome','fast-outcome','slow-arrival','slow-held')) {
        $state = Read-State (Join-Path $path "$name.dsave")
        if ((Get-Body $state).sprite.sectnum -ne 269) { throw "$character $name`: missed stock E1L1 shaft SE7; inspect $log" }
    }
    $arrival = Get-Body (Read-State (Join-Path $path 'slow-arrival.dsave'))
    if ([double]$arrival.sprite.pos[2] + [double]$arrival.viewzoffset -lt -116.0001) { throw "$character`: arrival eye remained inside the destination SE7 band" }
    if ($character -eq 'caleb') {
        $pending = 0
        foreach ($step in 1..8) {
            $state = Read-State (Join-Path $path "checkpoint-$step.dsave")
            $blood = @($state.objects | Where-Object classtype -eq 'BloodPlayer')[0]
            $previous = [double]$state['superset.blood_controller'].previous_eye_0
            if ((Get-Body $state).sprite.sectnum -eq 256 -and $previous -le -300 -and $blood.zview -ge -252) { $pending++ }
            foreach ($name in @("outcome-$step", "restored-$step")) {
                $after = Read-State (Join-Path $path "$name.dsave")
                if ((Get-Body $after).sprite.sectnum -ne 269) { throw "$character $name`: fall/save restore missed or returned through SE7; inspect $log" }
            }
        }
        if (!$pending) { throw "No pending crossed-window save captured; inspect $path" }
        Write-Output "Caleb: PASS (stock E1L1 roof fall, fast fall, saved pending crossings=$pending, eight restore timings, GC, no return teleport)"
    } else { Write-Output 'Duke: PASS (stock E1L1 roof and fast-fall transporter behavior preserved)' }
}
Write-Output "Artifacts: $root"
