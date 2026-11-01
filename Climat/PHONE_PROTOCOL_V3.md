# MCU ↔ телефон: протокол v3, 31.10.2026

Протокол v3 несовместим с v1/v2. Кадры прежних версий отклоняются VERSION=8
(ответ в версии 3); ни один старый SET_CONFIG не применяется. Остальные команды,
кроме HELLO/GET_CONFIG/SET_CONFIG, сохраняют payload v2. Новый Flash v5.
Машиночитаемый контракт: android_config_manifest_v3.json; эталоны tests/phone_vectors.json.

## Транспорт и кадр

F767 USART6 9600 8N1; USART3 текстовый 115200. BLE BT05/v6.1 peripheral,
телефон central, FFE0/FFE1. После соединения notify и HELLO. Границы BLE-порций
произвольные; полный эфирный путь требует проверки с приложением.

Провод: 00 + COBS(raw) + 00; max raw 71, payload 64, wire 74 байта.
raw: version:u8=3, opcode:u8, sequence:u16, length:u8, payload:N, CRC16:u16.
Little-endian, signed — two's complement. Ответ opcode OR 0x80.
CRC-16/CCITT-FALSE по version..payload: poly=0x1021, init=0xFFFF,
refin/refout=false, xorout=0; 123456789 → 0x29B1.
Пауза >=500 мс сбрасывает частичный кадр. Ошибки CRC/COBS/длины и кадры-ответы
игнорируются. После потери UART/переполнения восстановление по 00.

Один запрос в полёте; sequence uint16 с wrap. Повтор — тот же sequence и те же байты:
MCU кеширует последний запрос/ответ, возвращает его без повторного выполнения.
Тот же последний sequence с другим запросом — SEQUENCE. После reset кеш отсутствует;
не повторять старые операции после следующих. Рекомендованный timeout 4 с, до 2 повторов.
Чтение STATUS/SENSORS ~1 Гц. Автоматических событий v3 нет.

Первый байт ответа — status: OK=0, BAD_PAYLOAD=1, BAD_OP=2, BAD_CONFIG=3,
DENIED=4, STORAGE_ERROR=5, BUSY=6 (резерв), SEQUENCE=7, VERSION=8.
STORAGE_ERROR дополняется кодом EMPTY=1, INVALID=2, IO_ERROR=3.

## Команды

| Op | Команда | Запрос, байт | Ответ при успехе, байт |
|---:|---|---:|---:|
| 01 | HELLO | 0 | 8 |
| 02 | GET_STATUS | 0 | 23 |
| 03 | GET_SENSORS | 0 | 43 |
| 04 | GET_CONFIG | 0 | 40 |
| 05 | SET_CONFIG | 31 | 1 |
| 06 | SAVE | 0 | 2: status, unchanged |
| 07 | LOAD | 0 | 1 |
| 08 | DEFAULT | 0 | 1 |
| 09 | MODE | 1: 0 AUTO / 1 MANUAL | 6 |
| 0A | RELAY | 2: channel 1..4, on 0/1 | 6 |
| 0B | GET_STORAGE | 0 | 3: status, flash_state, ram_matches |
| 0C | GET_DEVICE_NAME | 0 | 4: status + 3 bytes |
| 0D | SET_DEVICE_NAME | 3 | 1 |
| 0E | GET_BINDINGS | 0 | 17: status + S1 ROM[8] + S2 ROM[8] |
| 0F | SET_BINDINGS | 16: S1 ROM[8], S2 ROM[8] | 1 |

HELLO offsets: 0=status, 1=version3, 2=max_payload64, 3=window5,
4=capabilities (bit0 locked, bit1 Flash, bit2 name, bit3 bindings),
5=Flash version5, 6=sensors2, 7=relays4. F767 caps14, F030 locked caps15.
Неизвестные биты игнорировать.

GET_STATUS offsets: 0=status; 1=uptime_ms:u32; 5=mode:u8; 6=source:u8
(0 none,1 S1,2 S2); 7=flags (bit0 fault, bit1 sampled, bit2 locked,
bit3 configured, bit4 достоверная производная AUTO); 8=requested_mask;
9=logical_mask; 10=physical_mask (GPIO ODR, не обратная связь контактов);
11=manual_remaining_ms:u32; 15=sample_age_ms:u32 (FFFFFFFF до результата);
19=измеренная температура AUTO:i32, 0,001 °C, без прогноза.

GET_SENSORS: status:u8, uptime_ms:u32, два блока по 19 байт.
В блоке: 0=flags (bit0 assigned,bit1 valid,bit2 trend_valid), 1=ROM[8],
9=raw:i16 в 1/16 °C, 11=rate:i32 в 0,001 °C/мин, 15=errors:u32.
Числа при valid/trend_valid=0 могут быть старыми; не использовать их.

SET_CONFIG, offsets без status: 0..15=8×i16 (h1on,h1hyst,h2on,h2hyst,
c1on,c1hyst,c2on,c2hyst), все в 1/16 °C; 16=speed:u16 в 0,001 °C/мин,
0=не настроено/отключено; 18=FAIL_COUNT:u8; 19=RECOVER_COUNT:u8;
20=alternate:u8; 21=MANUAL_TIMEOUT:u16 (1..3600 с).
Дополнительно SET_CONFIG: offset23 rising_rate:u16 (R), offset25 rate_hysteresis:u16 (H),
offset27 heater_min_on_seconds:u16, offset29 fan_min_on_seconds:u16. Всего 31 байт.
R/H: 0,001 °C/мин, диапазон 0..65535. R=0 — ограничение роста отключено.
H<=R при R>0 и H<=S при S>0; отключённый порог не ограничивает H.
MIN_ON каждой группы 1..600 секунд, независимо от фиксированного MIN_OFF.
GET_CONFIG: status + 31 байт SET_CONFIG (смещения полей больше на 1), затем readonly:
offset32 window:u16=5, offset34 min_h2_on:i16=168,
offset36 heater_min_off_seconds:u16=30, offset38 fan_min_off_seconds:u16=60.
Ответ всего 40 байт. Нельзя использовать прежние offsets 28/30 как readonly таймеры.

SET_CONFIG сохраняет отдельные имя/ROM, проверяет весь набор атомарно, применяет
в RAM и переводит в AUTO; неверный набор не меняет RAM.

SET_BINDINGS атомарно проверяет оба ROM: нули = unassigned, иначе family28 и CRC8,
дубликаты запрещены. Смена ролей очищает измерения/производную; AUTO, без SAVE.
GET_BINDINGS возвращает настроенные роли, включая отсутствующие датчики.
DEFAULT очищает роли и имя в RAM; LOAD восстанавливает их из Flash и переходит в AUTO.
SAVE — единственная запись; unchanged=1 не стирает Flash. LOAD при ошибке сохраняет RAM.

Имя — любые 3 байта; 000000 не назначено. SET_DEVICE_NAME не меняет режим,
не продлевает MANUAL. Для advertising CLM-XXXXXX нужны SAVE и reset.

Ответ MODE/RELAY: status, mode, requested, logical, fault, physical_locked.
Биты масок K1..K4. MANUAL немедленный, без MIN_ON/OFF, но с авариями/блокировками.
Зависимость K2 от K1 относится к AUTO. MANUAL продлевают только успешные MODE MANUAL
и RELAY; чтения, настройки и дубликаты не продлевают. Минимумы GET_CONFIG — для AUTO.
Авария двух датчиков: K1/K2 OFF,K3 ON,K4 OFF, выше временных ограничений.

Эталоны: tests/phone_vectors.json; независимый codec tests/phone_codec.ps1.
На F767 сохраняется стендовый PHONE TEST/HEX, отключённый после reset.

## Семантика новых настроек и Android

R: при достоверной d>=R блокировать запрос K2, в том числе температурную защёлку.
Разблокировка d<=R-H. S: ранний запрос при d<=-S, снятие d>=-S+H.
H=0: на совпадающей границе приоритет срабатывания: d=R блокирует K2,
d=-S сохраняет ранний запрос. Для возврата при H=0 нужны d<R и d>-S соответственно.
Недостоверная производная сбрасывает обе скоростные защёлки; температура управляет
как прежде. После смены AUTO-источника ожидание 5000 мс. Ошибка выбранного датчика
немедленно снимает нагрев; авария обоих/просрочка включает только K3 без MIN_ON.
MIN_ON ждёт арбитр, не таймер отложенной команды: после истечения учитывается
текущий запрос. MANUAL не ограничивается R/H/MIN_ON/MIN_OFF; авария/взаимная блокировка остаются.

В Android показать редактируемые R, общий H, MIN_ON нагревателей, MIN_ON вентиляторов;
MIN_OFF 30/60 — только чтение. Для R=0 явно показывать «Отключено». Делить R/H/S на 1000
для °C/мин, не считать производную на телефоне. Проверять H против включённых R/S,
отправлять весь SET_CONFIG атомарно, читать GET_CONFIG после успеха. SAVE отдельной командой;
после LOAD/DEFAULT обновлять поля. При VERSION предлагать совместимое приложение,
не отправлять автоматически кадры старой версии. Технические значения миграции R=H=0,
MIN_ON=30/60 сохраняют старое поведение; это не утверждённые заводские настройки.

Диапазон MIN_ON ограничен десятью минутами: не допускает многочасового удержания
выходов из-за uint16 максимума; 0 запрещён, немедленная наладка остаётся в MANUAL.
Расчёт миллисекунд uint32: максимум 600000, намного меньше половины периода wrap.
Обновление MIN_ON действует от времени последнего фактического переключения, таймер
не перезапускается. SET_CONFIG, как и v2, сбрасывает AUTO-защёлки и переводит в AUTO.
Текстовый CONFIG GET отображает новые значения; новые поля редактируются через v3 SET_CONFIG.
Старый AUTO SET сохраняет текущие R/H и отклоняет профиль, нарушающий H<=S/R.
