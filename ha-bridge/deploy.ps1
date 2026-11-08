# Копирует мостик на бокс HA и ставит службой. Секреты — из Bitwarden ($env), в stdin.
# Запуск из PowerShell с профилем (подгружает секреты): powershell -File deploy.ps1 [-Box 192.168.0.114]
param([string]$Box = '192.168.0.114')
$key = "$env:USERPROFILE\.ssh\id_ed25519_smarthome"
if (-not $env:SMARTHOME_HA_TOKEN -or -not $env:PANEL_4848_HOME_TOKEN) { 'нет SMARTHOME_HA_TOKEN или PANEL_4848_HOME_TOKEN'; exit 2 }
$sha = -join ([Security.Cryptography.SHA256]::Create().ComputeHash([Text.Encoding]::UTF8.GetBytes($env:PANEL_4848_HOME_TOKEN)) | ForEach-Object { $_.ToString('x2') })
ssh -i $key -o BatchMode=yes root@$Box 'mkdir -p /tmp/panel-bridge'
scp -i $key -q "$PSScriptRoot\bridge.py" "$PSScriptRoot\config.json" "$PSScriptRoot\install.sh" "root@${Box}:/tmp/panel-bridge/"
"$env:SMARTHOME_HA_TOKEN`n$sha" | ssh -i $key -o BatchMode=yes root@$Box "tr -d '\r' < /tmp/panel-bridge/install.sh > /tmp/panel-bridge/i.sh && bash /tmp/panel-bridge/i.sh; unlink /tmp/panel-bridge/i.sh"
