param([string]$Cli='arduino-cli',[string]$ConfigFile='', [switch]$ExamplesOnly,[switch]$CalibrationLab,[switch]$TouchStudy,[switch]$DiagnosticTelemetry,[switch]$FpsProbe,[switch]$UsbConsole)
$ErrorActionPreference='Stop'
if($CalibrationLab -and $TouchStudy){throw 'Choose calibration or sensitivity study, not both.'}
$repo=Split-Path $PSScriptRoot
$client=Join-Path $repo 'csos-client'
$sourceClient=$client
$scratchRoot=$null
try {
if($ExamplesOnly) {
    $scratchRoot=Join-Path ([IO.Path]::GetTempPath()) ('csos-examples-'+[guid]::NewGuid().ToString('N'))
    $client=Join-Path $scratchRoot 'csos-client'
    New-Item -ItemType Directory -Path $client -Force | Out-Null
    # Copy the sketch, never the operator's credentials or old build products.
    Get-ChildItem -LiteralPath $sourceClient -File | Where-Object Name -NotLike '*_private.h' |
        Copy-Item -Destination $client
    foreach($name in @('CloudConfig','ManagementConfig')) {
        Copy-Item -LiteralPath (Join-Path $client ($name+'.example.h')) -Destination (Join-Path $client ($name+'_private.h'))
    }
}
foreach($name in @('CloudConfig','ManagementConfig')) {
    $private=Join-Path $client ($name+'_private.h')
    if(!(Test-Path -LiteralPath $private)) {
        throw 'Run tools/configure.py with your local configuration first.'
    }
}
$arguments=@()
if($ConfigFile){$arguments+=@('--config-file',$ConfigFile)}
$buildName=if($TouchStudy){'build-touch-study'}elseif($CalibrationLab){'build-calibration-lab'}else{'build'}
if($ExamplesOnly){$buildName+='-examples'}
if($DiagnosticTelemetry){$buildName+='-diagnostic'}
if($FpsProbe){$buildName+='-fps-probe'}
$consoleEnabled=$UsbConsole -or $DiagnosticTelemetry -or $FpsProbe -or $CalibrationLab -or $TouchStudy
$cdcOnBoot=if($consoleEnabled){'cdc'}else{'default'}
if($UsbConsole){$buildName+='-usb-console'}
$fqbn="esp32:esp32:m5stack_cardputer:FlashSize=8M,PartitionScheme=custom,PSRAM=disabled,CDCOnBoot=$cdcOnBoot,DebugLevel=none"
$arguments+=@('compile','--fqbn',$fqbn,
 '--libraries',(Join-Path $repo 'vendor'),'--build-path',(Join-Path $sourceClient $buildName))
$defines=@()
if($consoleEnabled){$defines+='-DCSOS_USB_CONSOLE=1'}
if($CalibrationLab){$defines+='-DCSOS_CALIBRATION_LAB=1'}
if($TouchStudy){$defines+='-DCSOS_TOUCH_STUDY=1'}
if($DiagnosticTelemetry){$defines+='-DCSOS_DIAGNOSTIC_TELEMETRY=1'}
if($FpsProbe){$defines+='-DCSOS_FPS_PROBE=1'}
if($defines.Count){$arguments+=@('--build-property',('compiler.cpp.extra_flags='+($defines -join ' ')))}
$arguments+=$client
& $Cli @arguments
$buildExit=$LASTEXITCODE
} finally {
    if($scratchRoot -and (Test-Path -LiteralPath $scratchRoot)) {
        $resolved=(Resolve-Path -LiteralPath $scratchRoot).Path
        $allowed=Join-Path ([IO.Path]::GetTempPath()) 'csos-examples-'
        if(!$resolved.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)){throw 'Unexpected examples cleanup target'}
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
exit $buildExit
