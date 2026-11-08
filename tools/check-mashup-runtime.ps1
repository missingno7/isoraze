#Requires -Version 7.0
[CmdletBinding()]
param(
    [string]$ExePath = (Join-Path $PSScriptRoot '../build-mashup/Release/raze.exe'),
    [string]$DukeGrp,
    [string]$BloodRff,
    [string]$MashupBloodDirectory,
    [switch]$MashupCombat,
    [switch]$Superset,
    [ValidateSet('duke', 'caleb')][string]$SupersetPlayer = 'duke',
    [switch]$EnableSound,
    [string]$DukeTransitionMap,
    [string[]]$DukeMaps = @('E1L1', 'E2L1', 'E3L1', 'E4L1'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../build-mashup/checks'),
    [ValidateRange(10, 60)][int]$TimeoutSeconds = 45
)

$ErrorActionPreference = 'Stop'
if (!$DukeGrp -and !$BloodRff) { throw 'Specify -DukeGrp and/or -BloodRff.' }
if ($MashupCombat -and !$MashupBloodDirectory) { throw '-MashupCombat requires -MashupBloodDirectory.' }
if ($Superset -and !$MashupBloodDirectory) { throw '-Superset requires -MashupBloodDirectory.' }
if ($SupersetPlayer -eq 'caleb' -and !$Superset) { throw '-SupersetPlayer caleb requires -Superset.' }
if ($DukeTransitionMap -and $DukeTransitionMap -notmatch '^[a-zA-Z0-9_]+$') { throw 'Invalid transition map name.' }
$ExePath = (Resolve-Path -LiteralPath $ExePath).Path
if ($MashupBloodDirectory) {
    if (!$DukeGrp) { throw '-MashupBloodDirectory requires -DukeGrp.' }
    $MashupBloodDirectory = (Resolve-Path -LiteralPath $MashupBloodDirectory).Path
}
$cases = @()
if ($DukeGrp) {
    $DukeGrp = (Resolve-Path -LiteralPath $DukeGrp).Path
    foreach ($map in $DukeMaps) {
        if ($map -notmatch '^[a-zA-Z0-9_]+$') { throw "Invalid map name: $map" }
        $cases += @{ Name = "duke-$map"; Iwad = $DukeGrp; Map = $map }
    }
}
if ($BloodRff) {
    $BloodRff = (Resolve-Path -LiteralPath $BloodRff).Path
    $cases += @{ Name = 'blood-E1M1'; Iwad = $BloodRff; Map = 'E1M1' }
}

$runRoot = Join-Path $OutputDirectory ('runtime-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfff'))
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
$expected = 'Mashup runtime check: Duke=PASS, Blood=PASS, core=PASS, sector state=PASS, sector save=PASS, removal=PASS, cleanup=PASS.'
foreach ($case in $cases) {
    $caseRoot = Join-Path $runRoot $case.Name
    $saveRoot = Join-Path $caseRoot 'saves'
    $shotRoot = Join-Path $caseRoot 'shots'
    New-Item -ItemType Directory -Path $saveRoot, $shotRoot -Force | Out-Null
    $configPath = Join-Path $caseRoot 'raze.ini'
    $scriptPath = Join-Path $caseRoot 'check.cfg'
    $logPath = Join-Path $caseRoot 'raze.log'
    @'
[GlobalSettings]
vid_fullscreen=false
vid_preferbackend=1
vid_activeinbackground=true
i_pauseinbackground=false
vid_vsync=false
cl_nostartscreens=true
'@ | Set-Content -LiteralPath $configPath
    # A wait only defers the remainder of its command string. Keep this on one
    # line: separate lines in an exec file would run independently.
    $resourceCheck = if ($MashupBloodDirectory -and $case.Name.StartsWith('duke-')) { 'mashup_resource_check' } else { 'echo Ordinary game resource check' }
    $combatActive = ($MashupCombat -or ($Superset -and $SupersetPlayer -eq 'caleb')) -and $case.Name.StartsWith('duke-')
    $combatCheck = if ($combatActive) { 'mashup_combat_check' } else { 'echo Ordinary game combat check' }
    $movementCheck = if ($Superset -and $SupersetPlayer -eq 'caleb' -and $case.Name.StartsWith('duke-')) {
        'superset_movement_check start; +Jump; wait 2; -Jump; +Move_Forward; wait 4; -Move_Forward; wait 30; +Crouch; wait 6; -Crouch; wait 12; superset_movement_check'
    } else { 'echo Native movement retained' }
    $frontendCheck = if ($Superset -and $case.Name.StartsWith('duke-')) { 'printinterface; superset_check' } else { 'printinterface' }
    $alterPrototype = if ($Superset -and $case.Name.StartsWith('duke-')) {
        if ($MashupCombat) { 'mashup_combat 0' } else { 'mashup_combat 1' }
    } else { 'echo Native game selection retained' }
    $inputCheck = if ($combatActive) {
        'mashup_combat_input_check start; +Fire; wait 30; -Fire; wait 5; mashup_combat_input_check; mashup_combat_input_check start; +Alt_Fire; wait 30; -Alt_Fire; wait 5; mashup_combat_input_check'
    } else { 'echo Ordinary game input check' }
    $commands = @(
        'wait 90', 'mashup_runtime_check', 'wait 10',
        $frontendCheck,
        $resourceCheck,
        $movementCheck,
        $inputCheck,
        $combatCheck,
        'save foundation "Mashup foundation regression"', 'wait 30',
        $alterPrototype,
        'load foundation', 'wait 90', 'gc full', 'mashup_runtime_check',
        $frontendCheck,
        $resourceCheck,
        $movementCheck,
        $inputCheck,
        $combatCheck,
        'wait 15', 'screenshot', 'wait 10'
    )
    $expectedCount = 2
    if ($DukeTransitionMap -and $case.Name.StartsWith('duke-')) {
        $commands += @("map $DukeTransitionMap", 'wait 120', 'gc full', 'mashup_runtime_check', $frontendCheck, $resourceCheck, $movementCheck, $inputCheck, $combatCheck, 'wait 15', 'screenshot', 'wait 10')
        $expectedCount++
    }
    $commands += 'quit'
    $commands -join '; ' | Set-Content -LiteralPath $scriptPath

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $ExePath
    $startInfo.WorkingDirectory = Split-Path -Parent $ExePath
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    foreach ($argument in @(
        '-iwad', $case.Iwad, '-noautoload', '-noautoexec', '-nologo',
        '-nomusic', '-width', '800', '-height', '600',
        '-config', $configPath, '-savedir', $saveRoot, '-shotdir', $shotRoot,
        '-map', $case.Map, '+logfile', $logPath, '+iso_debug', '0', '+exec', $scriptPath
    )) { $startInfo.ArgumentList.Add($argument) }
    if (!$EnableSound) { $startInfo.ArgumentList.Add('-nosound') }
    if ($MashupBloodDirectory -and $case.Name.StartsWith('duke-')) {
        $preview = if ($MashupCombat -or $Superset) { '0' } else { '1' }
        $resourceArguments = if ($Superset) { @('-superset', '-blooddata', $MashupBloodDirectory, '-superset-player', $SupersetPlayer) } else { @('-bloodduke', $MashupBloodDirectory) }
        foreach ($argument in ($resourceArguments + @('+mashup_resource_preview', $preview))) {
            $startInfo.ArgumentList.Add($argument)
        }
        if ($MashupCombat) {
            $startInfo.ArgumentList.Add('+mashup_combat')
            $startInfo.ArgumentList.Add('1')
        }
    }
    $process = [Diagnostics.Process]::Start($startInfo)
    try {
        if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
            $process.Kill()
            $process.WaitForExit()
            throw "$($case.Name) timed out; inspect $logPath"
        }
        if ($process.ExitCode -ne 0) {
            throw "$($case.Name) exited with code $($process.ExitCode); inspect $logPath"
        }
    } finally { $process.Dispose() }

    $checks = @(Select-String -LiteralPath $logPath -SimpleMatch -Pattern $expected)
    if ($DukeTransitionMap -and $case.Name.StartsWith('duke-')) {
        if (!(Select-String -LiteralPath $logPath -Pattern "^${DukeTransitionMap}:")) {
            throw "$($case.Name) did not load $DukeTransitionMap; inspect $logPath"
        }
    }
    if ($MashupBloodDirectory -and $case.Name.StartsWith('duke-')) {
        $resourceChecks = @(Select-String -LiteralPath $logPath -SimpleMatch -Pattern 'Mashup resource check: isolation/colors=PASS, Caleb SEQ=PASS, weapon QAV=PASS, sprite save=PASS.')
        if ($resourceChecks.Count -ne $expectedCount) { throw "$($case.Name) failed resource checks; inspect $logPath" }
    }
    $save = Join-Path $saveRoot 'foundation.dsave'
    if ($Superset -and $case.Name.StartsWith('duke-')) {
        $frontendChecks = @(Select-String -LiteralPath $logPath -SimpleMatch -Pattern 'Superset check: identity/config=PASS, content manifest=PASS, save isolation=PASS, shared units=PASS.')
        if ($frontendChecks.Count -ne $expectedCount) { throw "$($case.Name) failed Superset frontend checks; inspect $logPath" }
        $archive = [IO.Compression.ZipFile]::OpenRead($save)
        try {
            $reader = [IO.StreamReader]::new($archive.GetEntry('info.json').Open())
            try { $metadata = $reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
            if ($metadata.Engine -ne 'Raze.Superset' -or $metadata.Superset.map_rules -ne 'duke' -or
                $metadata.Superset.duke_archive_md5.Length -ne 32 -or $metadata.Superset.blood_bank_md5.Length -ne 32) {
                throw "Invalid Superset save metadata in $save"
            }
            $reader = [IO.StreamReader]::new($archive.GetEntry('session.json').Open())
            try { $session = $reader.ReadToEnd() | ConvertFrom-Json -AsHashtable } finally { $reader.Dispose() }
            if ($session.'superset.gamestate'.pitchfork_prototype -ne [bool]$MashupCombat) {
                throw "Superset save did not retain prototype selection in $save"
            }
            $expectedController = if ($SupersetPlayer -eq 'caleb') { 1 } else { 0 }
            if ($session.'superset.gamestate'.player_controller -ne $expectedController) {
                throw "Superset save did not retain player selection in $save"
            }
        } finally { $archive.Dispose() }
        if (!(Select-String -LiteralPath $configPath -SimpleMatch -Pattern '[Superset.')) {
            throw "Superset configuration section missing in $configPath"
        }
    }
    if ($Superset -and $SupersetPlayer -eq 'caleb' -and $case.Name.StartsWith('duke-')) {
        $movementChecks = @(Select-String -LiteralPath $logPath -SimpleMatch -Pattern 'Superset movement check: native profile=PASS, move/jump/crouch=PASS, one collider=PASS, clock=PASS.')
        if ($movementChecks.Count -ne $expectedCount) { throw "$($case.Name) failed Caleb movement checks; inspect $logPath" }
    }
    if ($combatActive) {
        $combatChecks = @(Select-String -LiteralPath $logPath -SimpleMatch -Pattern 'Mashup combat check: player routing=PASS, native QAV damage=PASS, Duke ownership=PASS, self protection=PASS, sound bank=PASS.')
        if ($combatChecks.Count -ne $expectedCount) { throw "$($case.Name) failed combat checks; inspect $logPath" }
        $inputChecks = @(Select-String -LiteralPath $logPath -SimpleMatch -Pattern 'Mashup combat input check: Blood fire=PASS, Duke ammunition=PASS.')
        if ($inputChecks.Count -ne (2 * $expectedCount)) { throw "$($case.Name) failed real input checks; inspect $logPath" }
    }
    $shots = @(Get-ChildItem -LiteralPath $shotRoot -Filter '*.png')
    if ($checks.Count -ne $expectedCount -or !(Test-Path -LiteralPath $save) -or $shots.Count -eq 0) {
        throw "$($case.Name) did not complete runtime checks, save/load and screenshot; inspect $logPath"
    }
    Write-Output "$($case.Name): PASS (mixed actors, sector state, serialization, save/load)"
}
Write-Output "Artifacts: $runRoot"
