# Upload the built firmware over Wi-Fi. Needs the panel online: first 3 minutes after power-on on the
# scheduled-radio firmware, any time on panel-4848-idf. No --debug: espota prints the password with it.
param([string]$Target = 'panel-4848.local')
$fw = 'C:\pio21\w\panel-4848-idf\.pio\build\guition4848_idf\firmware.bin'
$espota = 'C:\pio21\packages\framework-arduinoespressif32\tools\espota.py'
if (-not $env:PANEL_4848_OTA_PASS) { 'PANEL_4848_OTA_PASS is empty'; exit 2 }
if (-not (Test-Path $fw)) { "no firmware: $fw"; exit 2 }
python $espota -i $Target -p 3232 "--auth=$env:PANEL_4848_OTA_PASS" -f $fw -r 2>&1 | Select-Object -Last 3 | ForEach-Object { "$_".Replace($env:PANEL_4848_OTA_PASS, '***') }
"exit: $LASTEXITCODE"
