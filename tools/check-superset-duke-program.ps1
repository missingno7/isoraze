#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$DukeGrp,
    [Parameter(Mandatory)][string]$BloodRff,
    [string]$ExePath = (Join-Path $PSScriptRoot '../build-mashup/Release/raze.exe'),
    [switch]$EnableSound,
    [ValidateRange(30, 180)][int]$TimeoutSeconds = 60,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../build-mashup/checks')
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'superset-check-common.ps1')
$ExePath = (Resolve-Path -LiteralPath $ExePath).Path
$DukeGrp = (Resolve-Path -LiteralPath $DukeGrp).Path
$BloodRff = (Resolve-Path -LiteralPath $BloodRff).Path
$runRoot = [IO.Path]::GetFullPath((Join-Path $OutputDirectory ('duke-program-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfff'))))
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
function Run-Case([string]$Name, [string[]]$Commands, [string]$Con = '', [switch]$Native) {
    $dir = Join-Path $runRoot $Name
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    $config = Join-Path $dir 'raze.ini'
    $script = Join-Path $dir 'check.cfg'
    $log = Join-Path $dir 'raze.log'
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
    $start.FileName = $ExePath
    $start.WorkingDirectory = Split-Path -Parent $ExePath
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $argsForCase = if ($Native) { @('-iwad', $DukeGrp, '-map', 'E1L1') } else {
        @('-iwad', $BloodRff, '-superset', '-dukedata', $DukeGrp, '-map', 'E1M1')
    }
    if ($Con) { $argsForCase += @('-dukecon', $Con) }
    $argsForCase += @('-noautoload', '-noautoexec', '-nologo', '-nomonsters', '-nomusic',
        '-config', $config, '-savedir', $dir, '-shotdir', $dir, '+logfile', $log, '+iso_debug', '0', '+exec', $script)
    if (!$EnableSound) { $argsForCase += '-nosound' }
    foreach ($arg in $argsForCase) { $start.ArgumentList.Add($arg) }
    $process = [Diagnostics.Process]::Start($start)
    try {
        if (!$process.WaitForExit($TimeoutSeconds * 1000)) { $process.Kill(); $process.WaitForExit(); throw "Duke program case $Name timed out; inspect $log" }
        if ($process.ExitCode -ne 0) { throw "Duke program case $Name exited $($process.ExitCode); inspect $log" }
    } finally { $process.Dispose() }
    $text = Get-Content -LiteralPath $log -Raw
    if ($text -match '\bFAIL\b|VM execution aborted') { throw "Duke program case $Name failed; inspect $log" }
    if (!$Native -and $text -notmatch 'program isolation: flags/players/map catalog/skills/quotes/sound registry=PASS') {
        throw "Missing program isolation check; inspect $log"
    }
    return $text
}
function Read-State([string]$Path) {
    $zip = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $result = @{}
        foreach ($entry in @('session', 'info')) {
            $reader = [IO.StreamReader]::new($zip.GetEntry("$entry.json").Open())
            try { $result[$entry] = $reader.ReadToEnd() | ConvertFrom-Json -AsHashtable } finally { $reader.Dispose() }
        }
        return $result
    } finally { $zip.Dispose() }
}
$stockCommands = @('wait 40', 'superset_duke_program_check', 'gc full', 'quit')
$primary = Run-Case native $stockCommands -Native
$secondary = Run-Case secondary $stockCommands
# Compare bytecode after native strength/header normalization and core startup values, not texture IDs or dynamically
# looked-up ZScript animation counts (which depend on the map adapter).
$pattern = 'code=(\w+) words=(\d+).*vars=(\d+) flags=(\w+) health=(\d+) ammo=(\d+) gravity=([\d.]+) queue=(\d+)'
$a = [regex]::Match($primary, $pattern)
$b = [regex]::Match($secondary, $pattern)
if (!$a.Success -or !$b.Success) { throw "Missing native/secondary program output; inspect $runRoot" }
foreach ($i in 1..8) { if ($a.Groups[$i].Value -ne $b.Groups[$i].Value) { throw "Native/secondary program mismatch in field $i; inspect $runRoot" } }

$fixture = Join-Path $runRoot 'fixture'
$relocated = Join-Path $runRoot 'relocated'
New-Item -ItemType Directory -Path $fixture, $relocated -Force | Out-Null
@'
include game.con
include probe.con
'@ | Set-Content -LiteralPath (Join-Path $fixture 'root.con')
@'
gamevar SUPERSET_PROBE_GLOBAL 9 0
gamevar SUPERSET_PROBE_PLAYER 11 1
onevent 0
    addvar SUPERSET_PROBE_GLOBAL 17
    addvar SUPERSET_PROBE_PLAYER 23
endevent
'@ | Set-Content -LiteralPath (Join-Path $fixture 'probe.con')
$initial = Run-Case fixture-run @('wait 40', 'superset_duke_program_check event', 'gc full',
    'save con-initial "Native CON event and private player state"', 'wait 4', 'superset_duke_program_check mutate',
    'save con-mutated "Changed native CON state"', 'wait 4', 'map E1M2', 'wait 40', 'load con-initial', 'wait 4', 'gc full',
    'superset_duke_program_check read', 'save con-restored "Native CON state restored after map and GC"', 'wait 4', 'quit') (Join-Path $fixture 'root.con')
if (@([regex]::Matches($initial, 'global=26 player0=34 player1=11 ammo=0 private_players=PASS')).Count -ne 2 -or
    $initial -notmatch 'global=999 player0=888 player1=11 ammo=77 private_players=PASS') {
    throw "Native CON event/per-player state failed; inspect $runRoot"
}
foreach ($name in @('initial', 'mutated', 'restored')) {
    $s = Read-State (Join-Path $runRoot "fixture-run/con-$name.dsave")
    $component = $s.session['superset.duke_program']
    if (!$component -or $component.players.Count -ne 8 -or @($component.players | Sort-Object -Unique).Count -ne 8 -or
        $s.info.Superset.duke_program_md5.Length -ne 32) { throw "Invalid saved secondary Duke component in $name" }
    foreach ($index in $component.players) {
        if ($s.session.objects[$index].classtype -ne 'DukePlayer' -or $s.session.objects[$index].Contains('actor')) {
            throw "Secondary Duke player replaced/duplicated the native Blood body in $name"
        }
    }
}
Copy-Item -LiteralPath (Join-Path $fixture 'root.con'), (Join-Path $fixture 'probe.con') -Destination $relocated
foreach ($caseName in @('relocated-run', 'rejected-run')) {
    $caseDir = Join-Path $runRoot $caseName
    New-Item -ItemType Directory -Path $caseDir -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $runRoot 'fixture-run/con-initial.dsave') -Destination $caseDir
}
$reload = Run-Case relocated-run @('wait 40', 'load con-initial', 'wait 4', 'gc full', 'superset_duke_program_check read',
    'save con-reloaded "Native CON state in a fresh process"', 'wait 4', 'quit') (Join-Path $relocated 'root.con')
if ($reload -notmatch 'global=26 player0=34 player1=11 ammo=0 private_players=PASS') { throw "Relocated CON content/fresh-process restore failed" }
Add-Content -LiteralPath (Join-Path $relocated 'probe.con') -Value '// Content fingerprint changed.'
$rejected = Run-Case rejected-run @('wait 40', 'map E1M2', 'wait 40', 'load con-initial', 'wait 4', 'superset_duke_program_check read',
    'quit') (Join-Path $relocated 'root.con')
# Native failed-load handling enters the console; do not request an in-level
# save there. Confirm metadata rejection precedes private state/map restoration.
if ($rejected -notmatch 'save requires a matching rules revision' -or
    $rejected -notmatch 'global=9 player0=11 player1=11 ammo=0 private_players=PASS map=e1m2') { throw "Changed CON content was not rejected cleanly" }
Write-Output 'Duke program: PASS (normalized native bytecode parity, isolated compiler outputs, native VM event, private players, save/GC/fresh process and content validation)'
Write-Output "Artifacts: $runRoot"
