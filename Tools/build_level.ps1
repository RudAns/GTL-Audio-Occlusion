# Rebuild the level from scratch: removes the old map file first (deleting it inside the editor trips a GC assert
# because the map references the exported Steam Audio assets), then runs build_level.py headless.
$p = Split-Path -Parent $PSScriptRoot
Remove-Item "$p\Content\Maps\SoundHouse.umap" -ErrorAction SilentlyContinue
$ue = Join-Path (& "$PSScriptRoot\find_ue.ps1") "Engine"
& "$ue\Binaries\Win64\UnrealEditor-Cmd.exe" "$p\AudioOcclusion.uproject" -run=pythonscript -script="$p\Tools\build_level.py" -unattended -nop4 -nosplash 2>&1 | Out-Null
Select-String -Path "$p\Saved\Logs\AudioOcclusion.log" -Pattern "LogSoundHouse|ALL DONE|Assertion" | ForEach-Object { $_.Line }
