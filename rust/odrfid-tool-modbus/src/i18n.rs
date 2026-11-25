//! English and Russian UI text. English strings are the keys.

use std::fmt::Display;
use std::sync::atomic::{AtomicBool, Ordering};

use crate::config::Language;

static RUSSIAN: AtomicBool = AtomicBool::new(false);

pub fn set_language(language: Language) {
    let ru = match language {
        Language::Ru => true,
        Language::En => false,
        Language::System => {
            sys_locale::get_locale().is_some_and(|l| l.to_ascii_lowercase().starts_with("ru"))
        }
    };
    RUSSIAN.store(ru, Ordering::Relaxed);
}

pub fn tr(en: &'static str) -> &'static str {
    if RUSSIAN.load(Ordering::Relaxed) {
        russian(en).unwrap_or(en)
    } else {
        en
    }
}

/// `tr` with each `{}` replaced by the next argument.
pub fn trf(en: &'static str, args: &[&dyn Display]) -> String {
    let mut out = String::new();
    let mut args = args.iter();
    let mut rest = tr(en);
    while let Some(at) = rest.find("{}") {
        out.push_str(&rest[..at]);
        match args.next() {
            Some(a) => out.push_str(&a.to_string()),
            None => out.push_str("{}"),
        }
        rest = &rest[at + 2..];
    }
    out.push_str(rest);
    out
}

fn russian(en: &str) -> Option<&'static str> {
    Some(match en {
        // The strip
        "Not connected" => "Не подключено",
        "Connecting…" => "Подключение…",
        "Connecting to {}…" => "Подключение к {}…",
        "Scanning addresses: {} of 247, found {}" => "Поиск по адресам: {} из 247, найдено {}",
        "Several readers answer: choose one below" => {
            "Отвечают несколько считывателей: выберите один ниже"
        }
        "No reader answers at address {} ({})" => "По адресу {} ({}) считыватель не отвечает",
        "No reader answered the scan" => "Ни один считыватель не ответил при поиске",
        "The RFID chip did not start" => "RFID-чип не запустился",
        "No card in the field" => "Карты в поле нет",
        "Autoscan is off" => "Автосканирование выключено",
        "Reading the reader state…" => "Чтение состояния считывателя…",
        "The reader does not answer: {}" => "Считыватель не отвечает: {}",
        "Port error: {}" => "Ошибка порта: {}",
        "No access to the serial port" => "Нет доступа к последовательному порту",
        "The port is used by another program" => "Порт занят другой программой",
        "Not supported: {}" => "Не поддерживается: {}",
        "The reader waits for the firmware in its bootloader" => {
            "Считыватель ждёт прошивку в загрузчике"
        }
        "address {}" => "адрес {}",
        "Disconnect" => "Отключиться",
        "Connect" => "Подключиться",
        "Read the settings again" => "Прочитать настройки заново",
        "Beep once" => "Подать сигнал",
        "Choose the port and connect" => "Выберите порт и подключитесь",
        "Check the wiring, the power and the bus settings" => {
            "Проверьте подключение, питание и параметры шины"
        }
        "Retrying…" => "Повторная попытка…",
        "See the Connection page" => "Подробности на странице «Подключение»",
        "Open the loader and upload the firmware within 30 s" => {
            "Откройте загрузчик и загрузите прошивку в течение 30 с"
        }
        "IN high" => "IN: 1",
        "IN low" => "IN: 0",
        "OUT on" => "OUT вкл.",
        "OUT off" => "OUT выкл.",
        "Green on" => "Зелёный вкл.",
        "Green off" => "Зелёный выкл.",
        "Red on" => "Красный вкл.",
        "Red off" => "Красный выкл.",
        "Beeping" => "Звучит зуммер",
        "Events: {}" => "Событий: {}",
        "Card events waiting in the reader's queue" => "События карт в очереди считывателя",
        "Odometer fault" => "Сбой счётчика наработки",
        "The operating time is not saved to EEPROM" => "Время работы не сохраняется в EEPROM",

        // Confirmations
        "Restart the reader?" => "Перезапустить считыватель?",
        "Settings that are not saved, the event queue and the last command are lost." => {
            "Несохранённые настройки, очередь событий и последняя команда будут потеряны."
        }
        "Restart" => "Перезапустить",
        "Restore the factory settings?" => "Вернуть заводские настройки?",
        "Keys, scanning and the bus go back to the defaults: 115200 8N1, address 1." => {
            "Ключи, сканирование и шина вернутся к заводским значениям: 115200 8N1, адрес 1."
        }
        "Reset" => "Сбросить",
        "Start the bootloader for a firmware update?" => {
            "Запустить загрузчик для обновления прошивки?"
        }
        "Modbus stops while the reader waits for the firmware. If no upload starts within 30 s, the reader returns to work." => {
            "Пока считыватель ждёт прошивку, Modbus не работает. Если загрузка не начнётся в течение 30 с, считыватель вернётся к работе."
        }
        "Start the bootloader" => "Запустить загрузчик",
        "Cancel" => "Отмена",

        // Pages
        "Reader" => "Считыватель",
        "Settings" => "Настройки",
        "Cards" => "Карты",
        "Tools" => "Сервис",
        "Card and events" => "Карта и события",
        "Frame log" => "Журнал обмена",
        "Application" => "Приложение",
        "Connect to a reader on the Connection page." => {
            "Подключитесь к считывателю на странице «Подключение»."
        }
        "Go to the Connection page" => "Перейти к подключению",
        "Not saved yet: {}" => "Не сохранено: {}",
        "The settings match the reader" => "Настройки совпадают со считывателем",
        "Results" => "Результаты",

        // Connection
        "Connection" => "Подключение",
        "Serial port" => "Последовательный порт",
        "Choose…" => "Выберите…",
        "No serial ports found" => "Последовательные порты не найдены",
        "Other:" => "Другой:",
        "Speed, baud" => "Скорость, бод",
        "Parity" => "Чётность",
        "None" => "Нет",
        "Odd" => "Нечёт",
        "Even" => "Чёт",
        "Stop bits" => "Стоп-биты",
        "The PC sends 2 stop bits for 1.5: serial ports have no 1.5" => {
            "Для 1,5 компьютер передаёт 2 стоп-бита: последовательные порты не умеют 1,5"
        }
        "Reader address" => "Адрес считывателя",
        "Scan all addresses" => "Искать по всем адресам",
        "Ask every address 1 to 247: about 20 s at 115200 baud, a minute and a half at 1200" => {
            "Опросить адреса с 1 по 247: на 115200 бод около 20 с, на 1200 — около полутора минут"
        }
        "Connect when the tool starts" => "Подключаться при запуске",
        "The connection settings changed" => "Параметры подключения изменены",
        "Stop" => "Остановить",
        "Connect again" => "Подключиться снова",
        "Devices found" => "Найденные устройства",
        "Address {} · S/N {} · firmware {}" => "Адрес {} · S/N {} · прошивка {}",
        "Address {} · {}" => "Адрес {} · {}",
        "Address {}" => "Адрес {}",
        "Use" => "Выбрать",
        "Linux opens serial ports for the dialout group (uucp on Arch). Add yourself to it, then log out and back in:" => {
            "В Linux последовательные порты доступны группе dialout (uucp в Arch). Добавьте себя в неё и войдите в систему заново:"
        }
        "Copy the command" => "Скопировать команду",
        "The command is copied to the clipboard" => "Команда скопирована в буфер обмена",
        "Close the other program (a terminal, another configuration tool) and connect again." => {
            "Закройте другую программу (терминал, другую утилиту настройки) и подключитесь снова."
        }
        "This tool works with ODRFID-485 readers with firmware 4.x. Configure readers with 3.x in ODRFIDConfigM." => {
            "Утилита работает со считывателями ODRFID-485 с прошивкой 4.x. Считыватели с 3.x настройте в ODRFIDConfigM."
        }

        // The reader
        "Model" => "Модель",
        "Firmware" => "Прошивка",
        "Serial number" => "Серийный номер",
        "not saved to EEPROM" => "не сохраняется в EEPROM",
        "Operating time" => "Наработка",
        "Starts" => "Запусков",
        "not counted: EEPROM write failed" => "не учтён: ошибка записи EEPROM",
        "RFID chip" => "RFID-чип",
        "Self-test at start" => "Самопроверка при запуске",
        "passed" => "пройдена",
        "Supply at start" => "Питание при запуске",
        "Last reset" => "Причина последнего сброса",
        "Self-test failed" => "Самопроверка не пройдена",
        "EEPROM write failed" => "запись в EEPROM не удалась",
        "{} mV" => "{} мВ",
        "not reported" => "не сообщается",
        "firewall" => "firewall",
        "option bytes loaded" => "загрузка option bytes",
        "NRST pin" => "вывод NRST",
        "power on or brownout" => "включение или провал питания",
        "software restart" => "программный перезапуск",
        "watchdog" => "сторожевой таймер",
        "window watchdog" => "оконный сторожевой таймер",
        "low-power mode exit" => "выход из режима пониженного потребления",
        "MFRC522 did not start" => "MFRC522 не запустился",
        "OK" => "OK",
        "{} d {} h {} min" => "{} д {} ч {} мин",
        "{} h {} min" => "{} ч {} мин",
        "{} min" => "{} мин",
        "{} min {} s" => "{} мин {} с",
        "{} s" => "{} с",

        // Settings
        "Scanning" => "Сканирование",
        "Autoscan" => "Автосканирование",
        "Find cards by itself and queue an event for each new one. Card operations of the command block need it off." => {
            "Считыватель сам ищет карты и ставит событие в очередь для каждой новой. Для операций с картой командного блока его нужно выключить."
        }
        "Switched on the Command block page until the reader restarts. The switch above shows the saved setting; saving keeps both." => {
            "Переключено на странице «Командный блок» до перезапуска считывателя. Переключатель выше показывает сохранённое значение; сохранение не меняет ни то, ни другое."
        }
        "Back to the saved setting" => "Вернуть сохранённое значение",
        "Scan period, ms" => "Период сканирования, мс",
        "Values below 100 ms count as 100 ms" => "Значения меньше 100 мс считаются как 100 мс",
        "Receiver gain, dB" => "Усиление приёмника, дБ",
        "Higher gain reads cards from further away but picks up more noise" => {
            "Большее усиление читает карты дальше, но ловит больше помех"
        }
        "Indication" => "Индикация",
        "Green LED shows a card in the field" => "Зелёный светодиод показывает карту в поле",
        "Turn it off to drive the green LED only from the Card page" => {
            "Выключите, чтобы управлять зелёным светодиодом только со страницы «Карта и события»"
        }
        "Beep for a new card" => "Сигнал при новой карте",
        "Also a 200 ms beep when the reader starts" => {
            "Также сигнал 200 мс при запуске считывателя"
        }
        "Autoread" => "Автоматическое чтение",
        "Blocks to read for each card" => "Блоков читать с каждой карты",
        "0 turns autoread off" => "0 выключает автоматическое чтение",
        "First block" => "Первый блок",
        "The autoscan reads them for each new card: at most 64 bytes, 4 Classic blocks or 16 Ultralight pages. Classic cards use the key on the Keys page." => {
            "Автоскан читает их с каждой новой карты: не больше 64 байт, 4 блока Classic или 16 страниц Ultralight. Для Classic используется ключ со страницы «Ключи»."
        }
        "Key type and 6 bytes: for autoread and for the block operations of the command block." => {
            "Тип ключа и 6 байт: для автоматического чтения и операций с блоками командного блока."
        }
        "4 bytes in card order." => "4 байта в порядке карты.",
        "16 bytes; Plus SL3 cards are authenticated with it before reading." => {
            "16 байт; карты Plus SL3 перед чтением проходят с ним аутентификацию."
        }
        "A change is applied first and saved only once the reader answers on the new settings." => {
            "Изменение сначала применяется и сохраняется, только когда считыватель ответит на новых параметрах."
        }
        "Keys" => "Ключи",
        "Mifare Classic key" => "Ключ Mifare Classic",
        "Ultralight EV1 / NTAG password" => "Пароль Ultralight EV1 / NTAG",
        "Mifare Plus AES key" => "Ключ AES Mifare Plus",
        "{} bytes in hex" => "{} байт в шестнадцатеричном виде",
        "RS-485 bus" => "Шина RS-485",
        "Address" => "Адрес",
        "End-of-frame pause, ms" => "Пауза конца кадра, мс",
        "0: the Modbus RTU minimum, 3.5 characters (1.75 ms from 19200 baud). The reader waits this long before every answer: at most 100 ms here." => {
            "0: минимум Modbus RTU, 3,5 символа (от 19200 бод — 1,75 мс). Считыватель выжидает эту паузу перед каждым ответом: здесь не больше 100 мс."
        }
        "The reader switches to {}, address {} at once and the tool follows. The settings are saved only if the reader answers there; otherwise power it off and on to return to the old ones." => {
            "Считыватель сразу перейдёт на {}, адрес {}, и утилита последует за ним. Настройки сохранятся, только если он там ответит; иначе выключите и включите питание, чтобы вернуть прежние."
        }
        "Saving…" => "Сохранение…",
        "A key is incomplete" => "Ключ введён не полностью",
        "Not saved yet" => "Не сохранено",
        "Save to the reader" => "Записать в считыватель",
        "Undo" => "Отменить",
        "Saving writes the settings to the reader's EEPROM; they work at once." => {
            "Запись сохраняет настройки в EEPROM считывателя; действуют они сразу."
        }
        "The address must be 1 to 247" => "Адрес должен быть от 1 до 247",
        "The speed must be a multiple of 100 from 500 to 2000000 baud" => {
            "Скорость должна быть кратна 100, от 500 до 2000000 бод"
        }
        "Autoread takes at most 16 blocks" => "Автоматическое чтение — не больше 16 блоков",

        // Maintenance
        "Maintenance" => "Обслуживание",
        "Restart the reader" => "Перезапустить считыватель",
        "Restart…" => "Перезапустить…",
        "Factory settings" => "Заводские настройки",
        "Reset…" => "Сбросить…",
        "Firmware update" => "Обновление прошивки",
        "Bootloader…" => "Загрузчик…",
        "The bootloader takes the firmware over the same RS-485 line. The tool closes the port: open the loader and upload the firmware. Without it the reader returns to work in 30 s." => {
            "Загрузчик принимает прошивку по той же линии RS-485. Утилита освобождает порт: откройте загрузчик и загрузите прошивку. Без загрузки считыватель вернётся к работе через 30 с."
        }

        // Cards
        "Type" => "Тип",
        "Copy" => "Копировать",
        "The UID is copied to the clipboard" => "UID скопирован в буфер обмена",
        "Memory" => "Память",
        "{} blocks × {} B" => "{} блоков × {} Б",
        " · {} blocks × {} B" => " · {} блоков × {} Б",
        "In the field" => "В поле",
        "yes" => "да",
        "not now" => "сейчас нет",
        "No card in the field." => "Карты в поле нет.",
        "No card selected: autoscan is off, select one with the command block." => {
            "Карта не выбрана: автосканирование выключено, выберите карту командным блоком."
        }
        "Off" => "Выкл.",
        "On" => "Вкл.",
        "Blink" => "Мигать",
        "OUT output" => "Выход OUT",
        "Green LED" => "Зелёный светодиод",
        "Red LED" => "Красный светодиод",
        "Beeper, continuous" => "Зуммер, непрерывно",
        "Beep pattern" => "Серия сигналов",
        "Beep" => "Подать",
        "IN input" => "Вход IN",
        "high" => "высокий",
        "low" => "низкий",
        "Manual indication works alongside the automatic LED and beeper; turn those off in the settings for full control." => {
            "Ручная индикация работает вместе с автоматической; для полного ручного управления выключите автоматические светодиод и зуммер в настройках."
        }
        "Card events" => "События карт",
        "The queue overflowed: older events were pushed out by newer ones." => {
            "Очередь переполнялась: старые события вытеснены новыми."
        }
        "The queue is empty. With autoscan on, each new card adds an event." => {
            "Очередь пуста. При включённом автосканировании каждая новая карта добавляет событие."
        }
        "Autoread: {}" => "Автоматическое чтение: {}",
        "Autoread: the card has no such blocks" => {
            "Автоматическое чтение: у карты нет таких блоков"
        }
        "Autoread failed: CME {}" => "Ошибка автоматического чтения: CME {}",
        "Acknowledge all" => "Подтвердить все",
        "Remove these events from the reader's queue" => {
            "Удалить эти события из очереди считывателя"
        }
        "Command block" => "Командный блок",
        "Autoscan is on: the reader rejects card operations." => {
            "Автосканирование включено: считыватель отклоняет операции с картой."
        }
        "Turn autoscan off until restart" => "Выключить автосканирование до перезапуска",
        "Operation" => "Операция",
        "block 0..255" => "блок 0..255",
        "block or key number" => "номер блока или ключа",
        "0 off, 1 on, 2 ask" => "0 выкл., 1 вкл., 2 запрос",
        "0 increment, 1 decrement" => "0 прибавить, 1 вычесть",
        "level 1..3" => "уровень 1..3",
        "UID, hex" => "UID, hex",
        "Operand" => "Операнд",
        "Data, hex" => "Данные, hex",
        "Running…" => "Выполняется…",
        "Run" => "Выполнить",

        // Operations
        "Version string" => "Строка версии",
        "MCU unique ID" => "Уникальный номер МК",
        "Firmware checksum (CRC32)" => "Контрольная сумма прошивки (CRC32)",
        "Enumerate cards in the field" => "Перебрать карты в поле",
        "Select the first card" => "Выбрать первую карту",
        "Select the next card" => "Выбрать следующую карту",
        "Check that a card is in the field" => "Проверить, что карта в поле",
        "Select a card by UID" => "Выбрать карту по UID",
        "Halt the card" => "Перевести карту в HALT",
        "Selected card info" => "Сведения о выбранной карте",
        "Antenna field" => "Поле антенны",
        "Read a block" => "Прочитать блок",
        "Read a Plus SL3 block" => "Прочитать блок Plus SL3",
        "Write a block" => "Записать блок",
        "Write a Plus block" => "Записать блок Plus",
        "Change a Classic value block" => "Изменить value-блок Classic",
        "Plus authentication" => "Аутентификация Plus",
        "Switch the Plus security level" => "Сменить уровень Plus",
        "Change the UID" => "Сменить UID",
        "Ultralight EV1 / NTAG PACK" => "PACK Ultralight EV1 / NTAG",

        // Command results
        "done" => "выполнено",
        "rejected" => "отклонено",
        "running" => "выполняется",
        "card error" => "ошибка карты",
        "no request yet" => "запросов ещё не было",
        "status {}" => "статус {}",
        "timeout" => "тайм-аут",
        "NAK" => "NAK",
        "authentication error" => "ошибка аутентификации",
        "communication error" => "ошибка связи",
        "no cards" => "карт нет",
        "field on" => "поле включено",
        "field off" => "поле выключено",
        "unknown type" => "неизвестный тип",
        "type {}" => "тип {}",

        // The frame log
        "Show polling" => "Показывать опрос",
        "Resume" => "Продолжить",
        "Pause" => "Пауза",
        "Save…" => "Сохранить…",
        "The log is copied to the clipboard" => "Журнал скопирован в буфер обмена",
        "Clear" => "Очистить",
        "No frames yet." => "Кадров пока нет.",
        "Save the frame log" => "Сохранить журнал обмена",
        "Saved to {}" => "Сохранено: {}",
        "Cannot write {}: {}" => "Ошибка записи в {}: {}",

        // Application
        "Language" => "Язык",
        "System" => "Системный",
        "Theme" => "Тема",
        "Light" => "Светлая",
        "Dark" => "Тёмная",
        "About" => "О программе",
        "OpenDev RFID configuration utility for ODRFID-485 readers with firmware 4.x over Modbus RTU." => {
            "Утилита настройки считывателей OD RFID: ODRFID-485 с прошивкой 4.x по Modbus RTU."
        }
        "Uses egui, serialport, the Inter font and Phosphor icons." => {
            "Использует egui, serialport, шрифт Inter и значки Phosphor."
        }
        "Settings file: {}" => "Файл настроек: {}",

        // Modbus
        "no answer" => "нет ответа",
        "bad answer" => "неверный ответ",
        "Modbus exception" => "Исключение Modbus",
        "illegal function" => "недопустимая функция",
        "illegal data address" => "недопустимый адрес данных",
        "illegal data value" => "недопустимое значение",
        "device failure" => "сбой устройства",
        "device busy" => "устройство занято",
        "exception" => "исключение",
        "the port is closed" => "порт закрыт",

        // Engine notices
        "The reader is not connected" => "Считыватель не подключён",
        "The indication did not change" => "Индикация не изменилась",
        "The beeper did not start" => "Зуммер не запустился",
        "The events were not acknowledged" => "События не подтверждены",
        "Autoscan did not change" => "Автосканирование не переключилось",
        "firmware 3.x: use ODRFIDConfigM" => "прошивка 3.x: настройте в ODRFIDConfigM",
        "a Modbus device that is not an ODRFID-485" => "устройство Modbus, но не ODRFID-485",
        "This reader cannot be used" => "С этим считывателем работать нельзя",
        "Connection restored" => "Связь восстановлена",
        "Cannot read the settings" => "Не удалось прочитать настройки",
        "The reader restarted" => "Считыватель перезапустился",
        "The reader restarted unexpectedly" => "Считыватель неожиданно перезапустился",
        "The reader restarted unexpectedly: {}" => "Считыватель неожиданно перезапустился: {}",
        "The settings have not been read yet" => "Настройки ещё не прочитаны",
        "Settings not written" => "Настройки не записаны",
        "Bus settings not written" => "Настройки шины не записаны",
        "The reader could not write its EEPROM: the settings work until it restarts" => {
            "Считыватель не смог записать EEPROM: настройки действуют до перезапуска"
        }
        "Settings not saved" => "Настройки не сохранены",
        "Settings saved; the reader now uses {}, address {}" => {
            "Настройки сохранены; считыватель работает на {}, адрес {}"
        }
        "Settings saved to the reader" => "Настройки записаны в считыватель",
        "The reader stored different settings" => "Считыватель сохранил другие настройки",
        "The bus settings were not applied" => "Настройки шины не применены",
        "The reader did not switch to the new bus settings" => {
            "Считыватель не перешёл на новые настройки шины"
        }
        "The reader does not answer at {}, address {}. Nothing was saved: power the reader off and on to return to {}, address {}." => {
            "Считыватель не отвечает на {}, адрес {}. Ничего не сохранено: выключите и включите питание считывателя, чтобы вернуться к {}, адрес {}."
        }
        "no answer after the bus change" => "нет ответа после смены параметров шины",
        "The reader is restarting" => "Считыватель перезапускается",
        "The reader did not restart" => "Считыватель не перезапустился",
        "The reader is in the RS-485 bootloader" => "Считыватель в загрузчике RS-485",
        "The reader did not enter the bootloader" => "Считыватель не перешёл в загрузчик",
        "The reader did not answer the restart request" => {
            "Считыватель не ответил на запрос перезапуска"
        }
        "The reader did not answer the bootloader request" => {
            "Считыватель не ответил на запрос перехода в загрузчик"
        }
        "The reader did not answer the factory reset. If it was reset, it answers at {}, address {}." => {
            "Считыватель не ответил на сброс к заводским настройкам. Если сброс прошёл, он отвечает на {}, адрес {}."
        }
        "The adapter cannot use {}: {}" => "Адаптер не работает на {}: {}",
        "Factory settings restored: {}, address {}" => {
            "Заводские настройки восстановлены: {}, адрес {}"
        }
        "The factory reset failed" => "Сброс к заводским настройкам не удался",
        "firmware {}: this tool needs 4.x" => "прошивка {}: нужна 4.x",
        "model {} is not an ODRFID-485" => "модель {} — не ODRFID-485",
        _ => return None,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    /// String literals passed to `tr(` and `trf(` in `text`.
    fn keys_in(text: &str) -> Vec<String> {
        let mut keys = Vec::new();
        for start in ["tr(", "trf("] {
            let mut rest = text;
            while let Some(at) = rest.find(start) {
                let before = rest[..at].chars().next_back();
                rest = &rest[at + start.len()..];
                if before.is_some_and(|c| c.is_alphanumeric() || c == '_') {
                    continue;
                }
                let trimmed = rest.trim_start();
                let Some(body) = trimmed.strip_prefix('"') else {
                    continue;
                };
                let mut key = String::new();
                let mut chars = body.chars();
                while let Some(c) = chars.next() {
                    match c {
                        '"' => break,
                        '\\' => {
                            if let Some(n) = chars.next() {
                                key.push(n);
                            }
                        }
                        c => key.push(c),
                    }
                }
                keys.push(key);
            }
        }
        keys
    }

    #[test]
    fn every_text_is_translated() {
        let sources = [
            include_str!("app.rs"),
            include_str!("engine.rs"),
            include_str!("modbus.rs"),
            include_str!("reader.rs"),
            include_str!("sysint.rs"),
        ];
        let mut keys: Vec<String> = sources.iter().flat_map(|s| keys_in(s)).collect();
        keys.extend(crate::reader::OPS.iter().map(|o| o.name.to_string()));
        keys.extend(crate::reader::RESET_CAUSES.map(String::from));
        // Page group headings, translated through a variable.
        keys.extend(["Settings", "Cards", "Tools"].map(String::from));
        assert!(keys.len() > 200, "{}", keys.len());
        let missing: Vec<&String> = keys.iter().filter(|k| russian(k).is_none()).collect();
        assert!(missing.is_empty(), "untranslated: {missing:#?}");
    }

    #[test]
    fn placeholders_survive_translation() {
        let keys = include_str!("i18n.rs")
            .lines()
            .filter_map(|l| l.trim().strip_prefix('"'))
            .filter_map(|l| l.split_once("\" => \""))
            .map(|(en, ru)| (en.to_string(), ru.trim_end_matches("\",").to_string()))
            .collect::<Vec<_>>();
        assert!(keys.len() > 100);
        for (en, ru) in keys {
            assert_eq!(en.matches("{}").count(), ru.matches("{}").count(), "{en}");
        }
    }

    #[test]
    fn formats() {
        assert_eq!(trf("address {}", &[&95]), "address 95");
        assert_eq!(trf("Address {} · {}", &[&1]), "Address 1 · {}");
    }
}
