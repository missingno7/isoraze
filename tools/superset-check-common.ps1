# Commands named save must be individual array entries. Each resulting phase
# waits for its ordinary input-stream save to finish before starting the next.
function Write-SupersetCheckScript([string[]]$Commands, [string]$ScriptPath) {
    $caseRoot = Split-Path -Parent $ScriptPath
    $phases = [Collections.Generic.List[object]]::new()
    $phase = [Collections.Generic.List[string]]::new()
    foreach ($command in $Commands) {
        $phase.Add($command)
        if ($command -like 'save *') { $phases.Add($phase); $phase = [Collections.Generic.List[string]]::new() }
    }
    if ($phase.Count) { $phases.Add($phase) }
    for ($i = 0; $i -lt $phases.Count; $i++) {
        $path = if (!$i) { $ScriptPath } else { Join-Path $caseRoot "phase-$i.cfg" }
        if ($i + 1 -lt $phases.Count) {
            $saveName = [regex]::Match($phases[$i][-1], '^save (\S+)').Groups[1].Value
            $nextPath = (Join-Path $caseRoot ('phase-' + ($i + 1) + '.cfg')).Replace('\', '/')
            $phases[$i].Add("wait 1; mashup_save_wait $saveName `"$nextPath`"")
        }
        $phases[$i] -join '; ' | Set-Content -LiteralPath $path
    }
}
