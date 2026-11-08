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
$root = [IO.Path]::GetFullPath((Join-Path $OutputDirectory ('view-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfff'))))
$cases = @(
    @{ Name = 'caleb-async'; Map = 'E1L1'; Character = 'caleb'; Sync = 'false' },
    @{ Name = 'caleb-sync'; Map = 'E1L1'; Character = 'caleb'; Sync = 'true' },
    @{ Name = 'duke-async'; Map = 'E1L1'; Character = 'duke'; Sync = 'false' },
    @{ Name = 'blood-async'; Map = 'E3M1'; Character = 'caleb'; Sync = 'false' }
)
foreach ($case in $cases) {
    $path = Join-Path $root $case.Name
    New-Item -ItemType Directory -Path $path -Force | Out-Null
    $config = Join-Path $path 'raze.ini'; $script = Join-Path $path 'check.cfg'; $log = Join-Path $path 'raze.log'
    @"
[GlobalSettings]
vid_fullscreen=false
vid_activeinbackground=true
i_pauseinbackground=false
vid_vsync=false
cl_nostartscreens=true
cl_noexitscreens=true
cl_loadingscreens=false
cl_syncinput=$($case.Sync)
cl_capfps=false
cl_slopetilting=false
in_mousemode=true
m_pitch=1
use_mouse=false
use_joystick=false
"@ | Set-Content -LiteralPath $config
    $commands = @('wait 90','superset_view_check',
        'superset_view_check center','wait 4','superset_view_check mouse 80','wait 4','superset_view_check',
        'wait 30','superset_view_check','save held-up',
        'superset_view_check mouse -160','wait 4','superset_view_check','wait 30','superset_view_check',
        'superset_view_check center','wait 30','superset_view_check',
        'load held-up','wait 8','gc full','superset_view_check',
        'superset_view_check mouse -32','wait 4','superset_view_check','wait 30','superset_view_check','quit')
    Write-SupersetCheckScript -Commands $commands -ScriptPath $script
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $ExePath; $start.WorkingDirectory = Split-Path -Parent $ExePath
    $start.UseShellExecute = $false; $start.CreateNoWindow = $true; $start.WindowStyle = 'Hidden'
    $worldArgs = if ($case.Map -eq 'E1L1') { @('-iwad',$DukeGrp,'-blooddata',(Split-Path -Parent $BloodRff)) }
        else { @('-iwad',$BloodRff,'-dukedata',$DukeGrp) }
    foreach ($argument in ($worldArgs + @('-superset','-superset-player',$case.Character,'-superset-weapons','blood',
        '-map',$case.Map,'-noautoload','-noautoexec','-nomonsters','-nomusic','-config',$config,'-savedir',$path,
        '+iso_debug','0','+logfile',$log,'+exec',$script))) { $start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::Start($start)
    try {
        if (!$process.WaitForExit(45000)) { $process.Kill(); $process.WaitForExit(); throw "$($case.Name) timed out; inspect $log" }
        if ($process.ExitCode) { throw "$($case.Name) exited $($process.ExitCode); inspect $log" }
    } finally { $process.Dispose() }
    $content = Get-Content -LiteralPath $log -Raw
    $samples = [regex]::Matches($content, 'Superset view: clock=\d+ body=([-\d.]+) native=([-\d.]+) camera=([-\d.]+) offset=([-\d.]+) center=(\d) input=([-\d.]+)')
    $expected = @(0,-5,-5,5,5,0,-5,-3,-3)
    if ($samples.Count -ne $expected.Count -or $content -match '\bFAIL\b|VM execution aborted') { throw "Missing/failed view probes; inspect $log" }
    for ($i = 0; $i -lt $expected.Count; $i++) {
        foreach ($group in 1,2,3) {
            $angle = [double]::Parse($samples[$i].Groups[$group].Value, [Globalization.CultureInfo]::InvariantCulture)
            if ([Math]::Abs($angle - $expected[$i]) -gt .001) { throw "$($case.Name) sample $i angle $group`: $angle, expected $($expected[$i]); inspect $log" }
        }
        if ($samples[$i].Groups[5].Value -ne '0') { throw "$($case.Name) sample $i`: centering latch remained set; inspect $log" }
    }
    Write-Output "$($case.Name): PASS (mouse up/down holds pitch, intentional centering, restored view/input after save/load/GC)"
}
Write-Output "Artifacts: $root"
