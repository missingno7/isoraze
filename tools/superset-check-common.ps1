function ConvertFrom-SupersetSnapshot([string]$Json) {
    $data = $Json | ConvertFrom-Json -AsHashtable
    # Compatibility aliases only in the regression reader. The engine saves
    # one inventory; older assertions can still compare the native views.
    foreach ($view in $data.objects) {
        if ($view['superset.inventory'] -isnot [long]) { continue }
        $inventory = $data.objects[$view['superset.inventory']]
        if ($view.classtype -eq 'BloodPlayer') {
            $view.ammocount = $inventory.blood_ammo; $view.hasweapon = $inventory.blood_weapons
        } elseif ($view.classtype -eq 'DukePlayer') {
            $view.ammo_count = $inventory.duke_ammo; $view.gotweapon = $inventory.duke_weapons
        }
    }
    return $data
}

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
