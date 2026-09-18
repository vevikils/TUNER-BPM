$workspace = 'C:\Users\alfaswz\Desktop\TUNER+BPM'
$pkgDir = Join-Path $workspace 'Release_Builds\Supreme_Tuner_BPM_Package'
$zipPath = Join-Path $workspace 'Release_Builds\Supreme_Tuner_BPM_v2.0.0_Windows.zip'

if (Test-Path $pkgDir) { Remove-Item $pkgDir -Recurse -Force }
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }

New-Item -ItemType Directory -Path (Join-Path $pkgDir 'VST3') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $pkgDir 'Standalone') -Force | Out-Null

Copy-Item -Path (Join-Path $workspace 'build\TunerBPMPlugin_artefacts\Release\VST3\Supreme Tuner BPM V.2.vst3') -Destination (Join-Path $pkgDir 'VST3') -Recurse -Force
Copy-Item -Path (Join-Path $workspace 'build\TunerBPMPlugin_artefacts\Release\Standalone\Supreme Tuner BPM V.2.exe') -Destination (Join-Path $pkgDir 'Standalone') -Force
Copy-Item -Path (Join-Path $workspace 'README.md') -Destination $pkgDir -Force
Copy-Item -Path (Join-Path $workspace 'LICENSE') -Destination $pkgDir -Force

Compress-Archive -Path (Join-Path $pkgDir '*') -DestinationPath $zipPath -Force
Remove-Item $pkgDir -Recurse -Force

Write-Host 'Package created successfully:'
Get-Item $zipPath | Select-Object Name, Length
