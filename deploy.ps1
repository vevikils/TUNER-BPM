$srcFile = "C:\Users\alfaswz\Desktop\TUNER+BPM\build\TunerBPMPlugin_artefacts\Release\VST3\Supreme Tuner BPM V.2.2.vst3\Contents\x86_64-win\Supreme Tuner BPM V.2.2.vst3"
$srcBundle = "C:\Users\alfaswz\Desktop\TUNER+BPM\build\TunerBPMPlugin_artefacts\Release\VST3\Supreme Tuner BPM V.2.2.vst3"
$srcExe = "C:\Users\alfaswz\Desktop\TUNER+BPM\build\TunerBPMPlugin_artefacts\Release\Standalone\Supreme Tuner BPM V.2.2.exe"

# Copy entire VST3 bundle to Common Files\VST3
$commonVst3 = "C:\Program Files\Common Files\VST3"
$destBundle = Join-Path $commonVst3 "Supreme Tuner BPM V.2.2.vst3"

if (Test-Path $destBundle) {
    try {
        Copy-Item -Path "$srcBundle\*" -Destination $destBundle -Recurse -Force -ErrorAction Stop
        Write-Host "Copied bundle contents to $destBundle"
    } catch {
        Write-Host "Could not directly overwrite bundle, updating binary directly..."
    }
} else {
    Copy-Item -Path $srcBundle -Destination $destBundle -Recurse -Force
    Write-Host "Created new bundle: $destBundle"
}

$targets = @(
    "C:\Program Files\Common Files\VST3\Supreme Tuner BPM V.2.2.vst3\Contents\x86_64-win\Supreme Tuner BPM V.2.2.vst3",
    "C:\Program Files\Common Files\VST3\Supreme Tuner BPM V.2.1.vst3\Contents\x86_64-win\Supreme Tuner BPM V.2.1.vst3",
    "C:\Program Files\Common Files\VST3\Supreme Tuner BPM V.2.vst3\Contents\x86_64-win\Supreme Tuner BPM V.2.vst3",
    "C:\Program Files\Common Files\VST3\STB2\STB2.vst3",
    "C:\Program Files\Common Files\VST3\STB2.vst3\Contents\x86_64-win\STB2.vst3",
    "C:\Program Files\Common Files\VST3\SupremeTunerBPM.vst3\Contents\x86_64-win\STB2.vst3",
    "C:\Program Files (x86)\VSTPlugIns\STB2.vst3\Contents\x86_64-win\STB2.vst3",
    "C:\Program Files\Common Files\VST3\STT2.vst3\Contents\x86_64-win\STT2.vst3"
)

foreach ($t in $targets) {
    $parent = Split-Path $t -Parent
    if (!(Test-Path $parent)) { 
        New-Item -ItemType Directory -Path $parent -Force | Out-Null 
    }
    if (Test-Path $t) {
        $suffix = [System.Guid]::NewGuid().ToString().Substring(0,8)
        $old = "$t.$suffix.old"
        try {
            Move-Item -Path $t -Destination $old -Force -ErrorAction Stop
            Write-Host "Renamed old file: $t -> $old"
        } catch {
            Write-Host "Could not move $($t)"
        }
    }
    try {
        Copy-Item -Path $srcFile -Destination $t -Force
        Write-Host "Successfully deployed to: $t"
    } catch {
        Write-Host "Failed to copy to $($t)"
    }
}

if (!(Test-Path ".\Output")) { New-Item -ItemType Directory -Path ".\Output" -Force | Out-Null }
Copy-Item -Path $srcExe -Destination ".\Output\Supreme Tuner BPM V.2.2.exe" -Force
Copy-Item -Path $srcFile -Destination ".\Output\Supreme Tuner BPM V.2.2.vst3" -Force
Copy-Item -Path $srcExe -Destination ".\Output\Supreme Tuner BPM V.2.1.exe" -Force
Copy-Item -Path $srcFile -Destination ".\Output\Supreme Tuner BPM V.2.1.vst3" -Force
Copy-Item -Path $srcExe -Destination ".\Output\STB2.exe" -Force
Copy-Item -Path $srcFile -Destination ".\Output\STB2.vst3" -Force
Write-Host "Deployment completed successfully."
