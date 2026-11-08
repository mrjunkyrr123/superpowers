# Сборка panel-4848-idf. Запуск: . $PROFILE; .\build.ps1 [ota | usb [COM10]]
#   ota — ещё и прошить по воздуху, usb — кабелем (460800).
#
# ESP-IDF не принимает пробелы в пути, а PlatformIO раскрывает ссылки до настоящего пути,
# поэтому исходники зеркалятся в C:\pio21\w. Общие файлы из firmware/src кладутся рядом с
# main.cpp: на втором этапе сборки pioarduino флаги путей (-I) не применяются.
# Ядро — pioarduino==6.2.0 в своём окружении, стандартный PlatformIO не трогается.
$ErrorActionPreference = 'Continue'
$venv = 'C:\pio21-venv'
$repo = Split-Path -Parent $PSScriptRoot
$w    = 'C:\pio21\w'
$log  = Join-Path $env:TEMP 'pio21-build.log'

if (-not (Test-Path "$venv\Scripts\python.exe")) {
  python -m venv $venv
  & "$venv\Scripts\python.exe" -m pip install -q 'pioarduino==6.2.0'
}
# Пароль OTA вшивается при сборке (platformio.ini: ${sysenv.PANEL_4848_OTA_PASS}). Без него
# прошивка выходит без OTA, и обратно — только кабелем (08.11.2026 так и вышло: собирал из
# оболочки без профиля). Секрет — из Bitwarden через $PROFILE.
if (-not $env:PANEL_4848_OTA_PASS) { 'нет PANEL_4848_OTA_PASS — запусти из PowerShell с профилем (. $PROFILE)'; exit 2 }
if ($args[0] -eq 'ota') { $envName = 'guition4848_idf_ota'; $target = '-t upload' }
elseif ($args[0] -eq 'usb') { $envName = 'guition4848_idf'; $target = '-t upload --upload-port ' + $(if ($args[1]) { $args[1] } else { 'COM10' }) }
else { $envName = 'guition4848_idf'; $target = '' }

robocopy "$repo\panel-4848\src" "$w\panel-4848\src" /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
robocopy "$repo\firmware\src" "$w\panel-4848\src" /XF main.cpp secrets.example.h panel_pins.h /NFL /NDL /NJH /NJS /NP | Out-Null
robocopy "$repo\panel-4848-idf" "$w\panel-4848-idf" /E /XD .pio /XF sdkconfig.* build.ps1 /NFL /NDL /NJH /NJS /NP | Out-Null

$env:PLATFORMIO_CORE_DIR = 'C:\pio21'
$env:PYTHONIOENCODING = 'utf-8'
# gcc и запуск процессов на этом ПК иногда падают сами: собранное остаётся, поэтому повтор.
for ($try = 1; $try -le 12; $try++) {
  $p = Start-Process "$venv\Scripts\python.exe" -ArgumentList "-m platformio run -d $w\panel-4848-idf -e $envName $target" -NoNewWindow -Wait -PassThru -RedirectStandardOutput $log -RedirectStandardError "$log.err"
  "try $try exit: $($p.ExitCode)"
  if ($p.ExitCode -eq 0) { break }
  if (-not (Select-String -Path $log, "$log.err" -Pattern '3221225477|Permission denied|Error 126' -Quiet)) { break }
}
# espota печатает пароль — в журнале секретов быть не должно.
$t = (Get-Content $log, "$log.err" -Raw -Encoding UTF8) -join "`n"
foreach ($s in (Get-ChildItem env: | Where-Object { $_.Name -match '(PASS|TOKEN|SECRET|KEY)' -and $_.Value.Length -ge 8 } | ForEach-Object { $_.Value })) { $t = $t.Replace($s, '***') }
[IO.File]::WriteAllText($log, $t, (New-Object Text.UTF8Encoding $false))
Remove-Item "$log.err" -ErrorAction SilentlyContinue
($t -split "`n") | Select-String ' error:|fatal error|FAILED|SUCCESS|Incompatible|RAM:|Flash:|Result attempt|No response|Unable to resolve' | Select-Object -Last 10 | ForEach-Object { $_.Line.Substring(0, [Math]::Min(180, $_.Line.Length)) }
"журнал: $log"
