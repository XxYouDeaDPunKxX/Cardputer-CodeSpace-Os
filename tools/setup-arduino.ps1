param([string]$Cli='arduino-cli')
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot
$cache=Join-Path $repo '.tools/arduino'
New-Item -ItemType Directory -Force $cache | Out-Null
$config=Join-Path $cache 'arduino-cli.yaml'
$data=(Join-Path $cache 'data').Replace('\','/')
$downloads=(Join-Path $cache 'downloads').Replace('\','/')
$user=(Join-Path $cache 'user').Replace('\','/')
@"
board_manager:
  additional_urls:
    - https://espressif.github.io/arduino-esp32/package_esp32_index.json
directories:
  data: $data
  downloads: $downloads
  user: $user
"@ | Set-Content -LiteralPath $config -Encoding utf8
foreach($command in @(
 @('core','update-index'),
 @('core','install','esp32:esp32@3.2.1'),
 @('lib','install','--no-deps','M5Cardputer@1.1.1','M5GFX@0.2.15','M5Unified@0.2.10','JPEGDEC@1.8.5','XPT2046_Touchscreen@1.4')
)){
 & $Cli --config-file $config @command
 if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
}
& python (Join-Path $PSScriptRoot 'apply-display-m5gfx-0.2.15.py') --library-root (Join-Path $user 'libraries/M5GFX') --apply
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
Write-Output "Ready. Build with tools/build.ps1 -ConfigFile `"$config`""
