#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$DukeGrp,
    [Parameter(Mandatory)][string]$BloodRff,
    [string]$PythonPath = 'python',
    [string]$ExePath = (Join-Path $PSScriptRoot '../build-mashup-dev/Release/raze.exe'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../build-mashup-dev/checks')
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'superset-check-common.ps1')
$ExePath = (Resolve-Path -LiteralPath $ExePath).Path
$DukeGrp = (Resolve-Path -LiteralPath $DukeGrp).Path
$BloodRff = (Resolve-Path -LiteralPath $BloodRff).Path
$root = [IO.Path]::GetFullPath((Join-Path $OutputDirectory ('object-world-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfff'))))
function Read-State([string]$Path) {
    $zip = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $reader = [IO.StreamReader]::new($zip.GetEntry('session.json').Open())
        try { return $reader.ReadToEnd() | ConvertFrom-Json -AsHashtable } finally { $reader.Dispose() }
    } finally { $zip.Dispose() }
}
foreach ($world in @('fixture','E1L1')) {
    $caseRoot = Join-Path $root $world
    New-Item -ItemType Directory -Path $caseRoot -Force | Out-Null
    $config = Join-Path $caseRoot 'raze.ini'; $script = Join-Path $caseRoot 'check.cfg'; $log = Join-Path $caseRoot 'raze.log'
    $map = $world
    $fileArgs = @()
    if ($world -eq 'fixture') {
        $mapFile = Join-Path $caseRoot 'objects.map'
        & $PythonPath (Join-Path $PSScriptRoot 'make-superset-navigation.py') $mapFile
        if ($LASTEXITCODE) { throw 'Map generation failed' }
        $map = 'objects.map'; $fileArgs = @('-file',$mapFile)
    }
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
    $commands = @('wait 100','god','save baseline')
    if ($world -eq 'fixture') {
        foreach ($case in @(@{name='floor';sector=5;speed=0},@{name='water-down';sector=7;speed=16},@{name='water-up';sector=8;speed=-16})) {
            $commands += @('load baseline','wait 2',"superset_object_world_check transport $($case.sector) $($case.speed)",
                'wait 1',"save $($case.name)", 'superset_object_world_check clear')
        }
        foreach ($case in @('glass','solid','blast')) {
            $commands += @('load baseline','wait 2',"superset_object_world_check $case",'wait 8','superset_object_world_check',
                "save $case", "load $case",'wait 4','gc full','superset_object_world_check',"save $case-restored")
        }
        $commands += @('load baseline','wait 2','superset_object_world_check lift','wait 2','save lift-moving',
            'wait 80','superset_object_world_check','save lift-finished','load lift-moving','wait 80','gc full',
            'superset_object_world_check','save lift-restored')
    } else {
        $commands += @('superset_object_world_check transport 256 80','wait 1','save air','load air','wait 2','gc full','save air-restored')
    }
    $commands += @('quit')
    Write-SupersetCheckScript -Commands $commands -ScriptPath $script
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $ExePath; $start.WorkingDirectory = Split-Path -Parent $ExePath
    $start.UseShellExecute = $false; $start.CreateNoWindow = $true; $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    foreach ($arg in ($fileArgs + @('-iwad',$DukeGrp,'-superset','-blooddata',(Split-Path -Parent $BloodRff),'-map',$map,
        '-superset-player','caleb','-superset-weapons','blood','-noautoload','-noautoexec','-nomonsters','-nomusic',
        '-config',$config,'-savedir',$caseRoot,'+iso_debug','0','+logfile',$log,'+exec',$script))) { $start.ArgumentList.Add($arg) }
    $process = [Diagnostics.Process]::Start($start)
    try {
        if (!$process.WaitForExit(180000)) { $process.Kill(); $process.WaitForExit(); throw "$world timed out; inspect $log" }
        if ($process.ExitCode -ne 0) { throw "$world exited $($process.ExitCode); inspect $log" }
    } finally { $process.Dispose() }
    if ((Get-Content -LiteralPath $log -Raw) -match '\bFAIL\b|VM execution aborted') { throw "Runtime failure; inspect $log" }
    $transfers = if ($world -eq 'fixture') { @(@{name='floor';sector=6},@{name='water-down';sector=8},@{name='water-up';sector=7}) }
        else { @(@{name='air';sector=269}) }
    foreach ($case in $transfers) {
        $data = Read-State (Join-Path $caseRoot "$($case.name).dsave")
        $actor = @($data.objects | Where-Object { $_.sprite.owner -eq 31044 })[0]
        if (!$actor -or $actor.sprite.sectnum -ne $case.sector -or $actor.owneractor -isnot [long]) {
            throw "$world $($case.name) did not transfer a live owned projectile; inspect $caseRoot"
        }
    }
    if ($world -eq 'fixture') {
        foreach ($phase in @('glass','glass-restored')) {
            $data = Read-State (Join-Path $caseRoot "$phase.dsave")
            foreach ($index in @(53,59)) { if ($data.engine.walls[$index].cstat -band 64) { throw "$phase`: glass still blocks hitscan" } }
        }
        if (([regex]::Matches((Get-Content -LiteralPath $log -Raw),'Superset object walls: glass=\d solid=1')).Count -lt 4) {
            throw "Solid/blast break handlers or save/restore failed; inspect $log"
        }
        if (([regex]::Matches((Get-Content -LiteralPath $log -Raw),'Superset object floor contact: PASS')).Count -ne 2) {
            throw "Resting thing did not follow the original moving lift; inspect $log"
        }
    }
    Write-Output "$world`: PASS (live object transport, original wall handlers, save/load/GC)"
}
Write-Output "Artifacts: $root"
