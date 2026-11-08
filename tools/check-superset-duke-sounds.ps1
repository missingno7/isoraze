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
$runRoot = [IO.Path]::GetFullPath((Join-Path $OutputDirectory ('duke-sounds-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfff'))))
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
$fixture = Join-Path $runRoot 'sounds.pk3'
$zip = [IO.Compression.ZipFile]::Open($fixture, [IO.Compression.ZipArchiveMode]::Create)
try {
    $entries = @{
        'sndinfo.txt' = @'
PISTOL_FIRE collision.wav
$resourceid PISTOL_FIRE 3
$volume PISTOL_FIRE 0.25
blood_fixture collision.wav
$resourceid blood_fixture 9999
$loop blood_fixture 0 11025
'@
        'zscript.txt' = @'
version "4.12"
class SupersetDukeSoundFixture : DukeBloodSplat1
{
    override void Initialize(DukeActor spawner)
    {
        Super.Initialize(spawner);
        self.PlayActorSound("GENERIC_AMBIENCE10");
    }
    static Sound Pistol() { return "PISTOL_FIRE"; }
    static Sound NumericPistol() { return Duke.FindSoundByResID(3); }
}
'@
    }
    foreach ($name in $entries.Keys) {
        $writer = [IO.StreamWriter]::new($zip.CreateEntry($name).Open(), [Text.UTF8Encoding]::new($false))
        try { $writer.Write($entries[$name]) } finally { $writer.Dispose() }
    }
    $writer = [IO.BinaryWriter]::new($zip.CreateEntry('collision.wav').Open())
    try {
        $writer.Write([Text.Encoding]::ASCII.GetBytes('RIFF')); $writer.Write([uint32](36 + 11025))
        $writer.Write([Text.Encoding]::ASCII.GetBytes('WAVEfmt ')); $writer.Write([uint32]16)
        $writer.Write([uint16]1); $writer.Write([uint16]1); $writer.Write([uint32]11025); $writer.Write([uint32]11025)
        $writer.Write([uint16]1); $writer.Write([uint16]8)
        $writer.Write([Text.Encoding]::ASCII.GetBytes('data')); $writer.Write([uint32]11025)
        $samples = [byte[]]::new(11025)
        for ($i = 0; $i -lt $samples.Length; ++$i) { $samples[$i] = [byte](128 + 20 * [Math]::Sin(2 * [Math]::PI * 220 * $i / 11025)) }
        $writer.Write($samples)
    } finally { $writer.Dispose() }
} finally { $zip.Dispose() }
function Run-Case([string]$Name, [string[]]$Commands) {
    $dir = Join-Path $runRoot $Name
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    $config = Join-Path $dir 'raze.ini'; $script = Join-Path $dir 'check.cfg'; $log = Join-Path $dir 'raze.log'
    @'
[GlobalSettings]
vid_fullscreen=false
vid_preferbackend=1
vid_activeinbackground=true
i_pauseinbackground=false
vid_vsync=false
cl_nostartscreens=true
'@ | Set-Content -LiteralPath $config
    Write-SupersetCheckScript -Commands $Commands -ScriptPath $script
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $ExePath; $start.WorkingDirectory = Split-Path -Parent $ExePath
    $start.UseShellExecute = $false; $start.CreateNoWindow = $true; $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    foreach ($arg in @('-iwad', $BloodRff, '-superset', '-dukedata', $DukeGrp, '-file', $fixture, '-noautoload', '-noautoexec',
        '-nologo', '-nomonsters', '-nomusic', '-config', $config, '-savedir', $dir, '-shotdir', $dir,
        '-map', 'E1M1', '+logfile', $log, '+iso_debug', '0', '+exec', $script)) { $start.ArgumentList.Add($arg) }
    $process = [Diagnostics.Process]::Start($start)
    try {
        if (!$process.WaitForExit($TimeoutSeconds * 1000)) { $process.Kill(); $process.WaitForExit(); throw "Duke sound case $Name timed out; inspect $log" }
        if ($process.ExitCode -ne 0) { throw "Duke sound case $Name exited $($process.ExitCode); inspect $log" }
    } finally { $process.Dispose() }
    $text = Get-Content -LiteralPath $log -Raw
    if ($text -match '\bFAIL\b|VM execution aborted') { throw "Duke sound case $Name failed; inspect $log" }
    return $text
}
$first = Run-Case first @('wait 60', 'superset_duke_projectile_check arena', 'superset_duke_sound_check definitions',
    'superset_duke_sound_check seed', 'wait 20', 'superset_duke_sound_check', 'superset_duke_sound_check move',
    'superset_duke_sound_check isolation', 'superset_duke_sound_check water',
    'save sound-start "Private Duke sounds alongside native Blood"', 'wait 4', 'gc full',
    'map E1M2', 'wait 40', 'load sound-start', 'wait 30', 'gc full', 'superset_duke_sound_check',
    'save sound-restored "Native sound channels restored after fresh map"', 'wait 4',
    'superset_duke_sound_check clear', 'gc full', 'save sound-cleared "Native actor sound cleanup"', 'wait 4', 'quit')
foreach ($text in @('private bytes=PASS, script names/numeric IDs=PASS, native properties=PASS, VOC decode=PASS, native playback=PASS',
    'Superset Duke sound sources: PASS', 'Superset Duke sound movement: PASS', 'Superset Duke sound isolation: PASS',
    'Superset Duke sound water: PASS', 'Superset Duke sound cleanup: PASS')) {
    if (!$first.Contains($text)) { throw "Missing check '$text'; inspect $runRoot" }
}
if (@([regex]::Matches($first, 'Superset Duke sound playing: PASS')).Count -ne 2) { throw 'Missing initial/restored backend channel check' }
$reloadDir = Join-Path $runRoot 'reload'
New-Item -ItemType Directory -Path $reloadDir -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $runRoot 'first/sound-start.dsave') -Destination $reloadDir
$reload = Run-Case reload @('wait 40', 'load sound-start', 'wait 30', 'gc full', 'superset_duke_sound_check',
    'save sound-reloaded "Private sound names restored in fresh process"', 'wait 4', 'superset_duke_sound_check clear', 'quit')
if (!$reload.Contains('Superset Duke sound playing: PASS') -or !$reload.Contains('Superset Duke sound cleanup: PASS')) { throw 'Fresh-process sound restore failed' }
function Read-State([string]$Path) {
    $zip = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $reader = [IO.StreamReader]::new($zip.GetEntry('session.json').Open())
        try { return $reader.ReadToEnd() | ConvertFrom-Json -AsHashtable } finally { $reader.Dispose() }
    } finally { $zip.Dispose() }
}
foreach ($path in @('first/sound-start.dsave', 'first/sound-restored.dsave', 'reload/sound-reloaded.dsave')) {
    $state = Read-State (Join-Path $runRoot $path)
    $private = @($state.sounds | Where-Object { ($_ | ConvertTo-Json -Compress) -match 'mashup/duke/' })
    if ($private.Count -ne 2) { throw "Expected two named private loops in $path; inspect $runRoot" }
}
$cleared = Read-State (Join-Path $runRoot 'first/sound-cleared.dsave')
if (@($cleared.sounds | Where-Object { ($_ | ConvertTo-Json -Compress) -match 'mashup/duke/' }).Count -or
    @($cleared.objects | Where-Object { $_.sprite.owner -in @(31034, 31035) }).Count) { throw 'Sound cleanup retained a channel or source' }
Write-Output 'Duke sounds: PASS (private VOC/IDs, compiled script names, native properties/playback, both source families, stop/query/pitch isolation, movement, water, save/GC/fresh process and cleanup)'
Write-Output "Artifacts: $runRoot"
