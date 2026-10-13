# Build (optional) and run the scripted self-test capture. Usage: powershell -File Tools/capture.ps1 [-Build] [-Only 03,04]
param([string]$Only = "", [string]$Extra = "", [switch]$Build, [int]$ResX = 1600, [int]$ResY = 900)
$p = Split-Path -Parent $PSScriptRoot
$ue = Join-Path (& "$PSScriptRoot\find_ue.ps1") "Engine"
$proj = "$p\AudioOcclusion.uproject"
if ($Build) {
    $b = & "$ue\Build\BatchFiles\Build.bat" AudioOcclusionEditor Win64 Development -Project="$proj" -WaitMutex 2>&1
    $b | Select-String -Pattern "error C|error LNK|Result:" | ForEach-Object { $_.Line }
    if (-not ($b | Select-String -Pattern "Result: Succeeded")) { "BUILD FAILED - stopping"; exit 1 }
}
$out = "$p\Saved\Capture"
New-Item -ItemType Directory -Force $out | Out-Null
Get-ChildItem $out -File | Remove-Item -Force
$args = @("`"$proj`"", "-game", "-windowed", "-ResX=$ResX", "-ResY=$ResY", "-OccCapture=$out", "-RenderOffScreen", "-ForceRes", "-unattended", "-nosplash", "-log=capture.log")
if ($Extra) { $args += ($Extra -split " ") }
if ($Only) { $args += ("-OccOnly=" + (($Only -replace "[ ,]", "+"))) }
$proc = Start-Process -FilePath "$ue\Binaries\Win64\UnrealEditor.exe" -ArgumentList $args -PassThru
if (-not $proc.WaitForExit(1500000)) { $proc.Kill(); "capture timeout" }
Get-ChildItem $out | ForEach-Object { $_.Name }
