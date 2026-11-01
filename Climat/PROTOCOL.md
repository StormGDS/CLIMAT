# Актуальные интерфейсы Climat

Текущий бинарный контракт — [PHONE_PROTOCOL_V3.md](PHONE_PROTOCOL_V3.md).
Версии v1/v2 несовместимы с новым смыслом настроек и не выполняется MCU.
Прежняя история — docs/history/PROTOCOL_before_2026-10-30.md и PHONE_PROTOCOL_V1.md.
Консоль F767 USART3 115200 8N1; телефон USART6 9600 8N1. F030 — service strap
для текстового режима, иначе бинарный канал; предварительная разводка LOCKED.

AUTO SET h1on h1hyst h2on h2hyst c1on c1hyst c2on c2hyst speed

Первые 8 целых — 1/16 °C; speed — положительная скорость в 0,001 °C/мин,
0 отключает досрочный K2. AUTO GET выводит этот же порядок. Целевой уставки
и горизонта прогноза нет. AUTO ALTERNATE ON/OFF управляет чередованием.

SENSORS показывает роли и обнаруженные ROM строками FOUND. Назначение:
SENSOR BIND 1 ROMHEX16 / SENSOR BIND 2 ROMHEX16; очистка SENSOR CLEAR 1|2.
Команды применяют RAM и AUTO; затем SAVE для сохранения после reset.
Неверные ROM/дубликаты не применяются. Нет автоматического назначения ролей.

Сохранены HELP, STATUS, RELAYS, STREAM, CONFIG GET/SET/STORAGE, SAVE, LOAD,
DEFAULT, MODE AUTO/MANUAL, RELAY n ON/OFF, DEVICE_NAME/DEVICE_NAME_HEX.
F030 использует краткие диагностические ответы; синтаксис команд и бинарный
контракт одинаковы. Формат хранения и миграция — STAGE4_FLASH.md.

Обновление 31.10.2026: R/Hrate и MIN_ON двух групп передаются через v3 SET_CONFIG.
Точные поля/границы — PHONE_PROTOCOL_V3.md, android_config_manifest_v3.json.
R=Hrate=0 и MIN_ON=30/60 — совместимые значения миграции. Запрет роста перекрывает
температурную защёлку K2, выключение ждёт MIN_ON и зависит от текущего запрета.
CONFIG GET показывает новые значения; старый AUTO SET сохраняет R/Hrate.
Android и HTML в этой задаче не изменялись. Отчёт: TEST_REPORT_2026-10-31.md.
