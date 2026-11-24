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

/// A decimal with the separator of the language: `21.3` or `21,3`.
pub fn number(value: f32, decimals: usize) -> String {
    let text = format!("{value:.decimals$}");
    if RUSSIAN.load(Ordering::Relaxed) {
        text.replace('.', ",")
    } else {
        text
    }
}

fn russian(en: &str) -> Option<&'static str> {
    Some(match en {
        // State and the strip
        "Heartbeat sent just now" => "Сигнал отправлен только что",
        "Heartbeat sent {} s ago" => "Сигнал отправлен {} с назад",
        "Connecting…" => "Подключение…",
        "Looking for the watchdog…" => "Поиск устройства…",
        "No watchdog connected" => "Watchdog не подключён",
        "Watchdog {} is not connected" => "Watchdog {} не подключён",
        "No access to the watchdog" => "Нет доступа к устройству",
        "Device error: {}" => "Ошибка устройства: {}",
        "Unsupported firmware {}" => "Неподдерживаемая прошивка {}",
        "Timer suspended: the PC will not be restarted" => {
            "Таймер остановлен: ПК не будет перезагружен"
        }
        "Hide to the tray" => "Свернуть в трей",
        "Quit" => "Выход",
        "Settings" => "Настройки",
        "Shut down the PC" => "Выключить ПК",
        "Hard reset: power-cycle the PC" => "Жёсткая перезагрузка ПК (по питанию)",
        "Reset the PC" => "Перезагрузить ПК",
        "Green LED blinks on each heartbeat" => "Зелёный индикатор мигает при каждом сигнале",
        "Green LED is off" => "Зелёный индикатор выключен",
        "How to fix" => "Как исправить",
        "Plug the watchdog into a USB port" => "Подключите Watchdog к USB-порту",
        "Retrying…" => "Повторная попытка…",
        "Reading the device state…" => "Чтение состояния…",
        "CH{} closed" => "Кан.{} закрыт",
        "CH{} open" => "Кан.{} открыт",
        "Sensor n/a" => "Датчик н/д",
        "The DS18B20 sensor does not answer" => "Датчик DS18B20 не отвечает",
        "IN high" => "Вход: 1",
        "IN low" => "Вход: 0",
        "OUT high" => "Выход: 1",
        "OUT low" => "Выход: 0",
        "The watchdog acts after this long without heartbeats" => {
            "Через столько времени без сигналов Watchdog перезагрузит ПК"
        }
        "Timer off" => "Таймер выключен",
        "The device settings leave the watchdog unable to restart the PC" => {
            "С текущими настройками устройство не может перезагрузить ПК"
        }
        "Device fault" => "Сбой устройства",
        "ADC fault: supply and MCU temperature are not measured" => {
            "Сбой АЦП: питание и температура МК не измеряются"
        }
        "Settings could not be saved to flash" => "Не удалось сохранить настройки во flash-память",
        "The operating time counter is damaged" => "Повреждён счётчик времени работы",

        // Confirmations
        "Reset the PC now?" => "Перезагрузить ПК сейчас?",
        "The watchdog presses the reset button of this PC." => {
            "Watchdog нажмёт кнопку Reset этого ПК."
        }
        "Reset" => "Перезагрузить",
        "Power-cycle the PC now?" => "Жёстко перезагрузить ПК сейчас?",
        "The power is cut and restored; unsaved work is lost." => {
            "Питание будет отключено и включено снова; несохранённые данные пропадут."
        }
        "Hard reset" => "Жёсткая перезагрузка",
        "Shut down the PC now?" => "Выключить ПК сейчас?",
        "The watchdog holds the power button until the PC turns off." => {
            "Watchdog удержит кнопку питания, пока ПК не выключится."
        }
        "Shut down" => "Выключить",
        "Run the self-test?" => "Запустить самотест?",
        "LEDs blink, CH1 and CH2 pulse for 200 ms: wired channels act on the PC." => {
            "Мигнут индикаторы, на Кан.1 и Кан.2 будут импульсы по 200 мс: подключённые каналы подействуют на ПК."
        }
        "Run" => "Запустить",
        "Quit while the timer is running?" => "Выйти, пока таймер работает?",
        "Without heartbeats the watchdog restarts this PC in {} min." => {
            "Без сигналов Watchdog перезагрузит этот ПК через {} мин."
        }
        "Without heartbeats the watchdog restarts this PC." => {
            "Без сигналов Watchdog перезагрузит этот ПК."
        }
        "Suspend the timer and quit" => "Остановить таймер и выйти",
        "Quit anyway" => "Всё равно выйти",
        "Cancel" => "Отмена",

        // Drawer
        "Watchdog" => "Watchdog",
        "Device" => "Устройство",
        "Auto: the first one found" => "Авто: первый найденный",
        "not connected" => "не подключён",
        "Scan" => "Искать заново",
        "Serial number" => "Серийный номер",
        "Firmware" => "Прошивка",
        "off" => "выкл.",
        "{} min" => "{} мин",
        "Channels" => "Каналы",
        "reset" => "перезагрузка",
        "power" => "питание",
        "output, open" => "выход, разомкнут",
        "output, closed" => "выход, замкнут",
        "input" => "вход",
        "output" => "выход",
        "DS18B20 sensor" => "датчик DS18B20",
        "MCU sensor" => "датчик МК",
        "Temperature limit" => "Порог температуры",
        "Self-check" => "Самодиагностика",
        "OK" => "OK",
        "OK, {} ADC errors recovered" => "OK, исправлено ошибок АЦП: {}",

        // Device settings
        "Device settings" => "Настройки устройства",
        "This firmware cannot change settings over USB HID. Update the watchdog firmware, or change them in wdtmon3 over the serial port." => {
            "Эта прошивка не принимает настройки через USB HID. Обновите прошивку Watchdog или измените настройки в wdtmon3 через последовательный порт."
        }
        "Restart after no heartbeat for" => "Перезагрузка, если нет сигнала",
        "Channel 1" => "Канал 1",
        "Channel 2" => "Канал 2",
        "Reset button press" => "Нажатие кнопки Reset",
        "Power off: button hold" => "Выключение: удержание кнопки",
        "Pause before power on" => "Пауза перед включением",
        "Power on: button press" => "Включение: нажатие кнопки",
        "Restarts before turning the PC off" => "Перезагрузок до выключения ПК",
        "Restarts in a row that bring no heartbeat back; then the watchdog turns the PC off" => {
            "Столько перезагрузок подряд без восстановления сигнала, после чего Watchdog выключает ПК"
        }
        "unlimited" => "без лимита",
        "IN/TEMP channel" => "Канал IN/TEMP",
        "Reaching it acts like a missed heartbeat" => {
            "При достижении Watchdog действует так же, как при пропаже сигнала"
        }
        "Measured by" => "Датчик",
        "{} s" => "{} с",
        "{} °C" => "{} °C",
        "The timer is off: the watchdog never restarts the PC." => {
            "Таймер выключен: Watchdog не перезагрузит ПК."
        }
        "Neither channel presses Reset or Power: the timer cannot restart the PC." => {
            "Ни один канал не нажимает Reset или кнопку питания: таймер не сможет перезагрузить ПК."
        }
        "The temperature limit needs the IN/TEMP channel in DS18B20 sensor mode." => {
            "Для порога температуры канал IN/TEMP должен работать с датчиком DS18B20."
        }
        "The MCU sensor reads the chip itself: within ±5 °C and warmer than the air, so leave a margin." => {
            "Датчик МК измеряет температуру самого чипа: погрешность до ±5 °C, и он теплее воздуха, поэтому задайте порог с запасом."
        }
        "Saving…" => "Сохранение…",
        "Not saved yet" => "Не сохранено",
        "Save to the watchdog" => "Записать в Watchdog",
        "Undo" => "Отменить",
        "Defaults" => "По умолчанию",
        "The factory settings; nothing is written until you save" => {
            "Заводские настройки; в устройство они попадут только после записи"
        }
        "Saving restarts the device timer." => "После записи таймер устройства запускается заново.",
        "The previous settings are still being saved" => "Предыдущие настройки ещё сохраняются",
        "The watchdog rejected the settings" => "Watchdog отклонил настройки",
        "The watchdog did not accept the settings" => "Watchdog не принял настройки",
        "Settings not saved: {}" => "Настройки не сохранены: {}",
        "The watchdog is busy with a restart; the settings were not saved" => {
            "Watchdog занят перезагрузкой ПК, настройки не сохранены"
        }
        "The watchdog could not write its flash memory" => "Watchdog не смог записать flash-память",
        "The watchdog reported an unknown result" => "Watchdog сообщил неизвестный результат",
        "The watchdog did not confirm the settings" => "Watchdog не подтвердил запись настроек",
        "Watchdog settings saved: {}" => "Настройки Watchdog сохранены: {}",
        "Settings saved to the watchdog" => "Настройки записаны в Watchdog",
        "The watchdog stored different settings" => "Watchdog сохранил не те настройки",
        "Blink the green LED on heartbeats" => "Мигать зелёным индикатором при сигналах",
        "Suspend the watchdog timer" => "Остановить таймер Watchdog",
        "While the timer is suspended, the watchdog does not restart a hung PC." => {
            "Пока таймер остановлен, Watchdog не перезагрузит зависший ПК."
        }
        "Self-test" => "Самотест",
        "Run…" => "Запустить…",
        "Web server" => "Веб-сервер",
        "Status page and JSON API" => "Страница состояния и JSON API",
        "Port" => "Порт",
        "Open in the browser" => "Открыть в браузере",
        "also open to the network" => "доступен также из сети",
        "The server cannot start: {}" => "Сервер не запускается: {}",
        "Log" => "Журнал",
        "Write events to a file" => "Записывать события в файл",
        "File size limit, MB" => "Размер файла, МБ",
        "Old files to keep" => "Хранить старых файлов",
        "Open the log" => "Открыть журнал",
        "Open the folder" => "Открыть папку",
        "The log file does not exist yet" => "Файл журнала ещё не создан",
        "Application" => "Приложение",
        "Language" => "Язык",
        "System" => "Системный",
        "Theme" => "Тема",
        "Light" => "Светлая",
        "Dark" => "Тёмная",
        "Start with the system" => "Запускать вместе с системой",
        "Autostart: {}" => "Автозапуск: {}",
        "Start hidden in the tray" => "Запускать свёрнутым в трей",
        "Keep above other windows" => "Поверх других окон",
        "Ask before quitting" => "Спрашивать перед выходом",
        "About" => "О программе",
        "Heartbeat monitor for USB Watchdog Pro2 and Lite over USB HID: no driver or serial port needed." => {
            "Монитор USB Watchdog Pro2 и Lite через USB HID: без драйвера и последовательного порта."
        }
        "PC uptime {} · monitor uptime {}" => "ПК работает {} · монитор работает {}",
        "Linux lets only root open USB HID devices unless a udev rule allows it. Install the rule with this command, then re-plug the watchdog:" => {
            "Без правила udev Linux разрешает открывать USB HID-устройства только root. Установите правило этой командой и переподключите Watchdog:"
        }
        "Copy the command" => "Скопировать команду",
        "The command is copied to the clipboard" => "Команда скопирована в буфер обмена",
        "Cannot prepare the rule file: {}" => "Не удалось подготовить файл правила: {}",
        "Another program may hold the device, or the system denies access to it." => {
            "Устройство занято другой программой, или система запрещает доступ к нему."
        }
        "{} d {} h {} min" => "{} д {} ч {} мин",
        "{} h {} min" => "{} ч {} мин",
        "{} min {} s" => "{} мин {} с",

        // Tray
        "Show" => "Показать",
        "Hide" => "Скрыть",
        "Test Reset" => "Тест перезагрузки",
        "Test Hard Reset" => "Тест жёсткой перезагрузки",
        "heartbeats are being sent" => "сигналы отправляются",
        "the watchdog timer is suspended" => "таймер Watchdog остановлен",
        "no connection to the watchdog" => "нет связи с устройством",
        "starting" => "запуск",

        // Log and notices
        "Application started" => "Приложение запущено",
        "Application closed" => "Приложение закрыто",
        "Detected watchdog {}" => "Найден Watchdog {}",
        "The watchdog was disconnected" => "Watchdog отключён",
        "No connected watchdogs detected" => "Подключённые устройства не найдены",
        "No access to watchdog {}" => "Нет доступа к Watchdog {}",
        "Cannot open watchdog {}: {}" => "Не удалось открыть Watchdog {}: {}",
        "Watchdog {} does not answer: {}" => "Watchdog {} не отвечает: {}",
        "Unsupported device {} ({})" => "Неподдерживаемое устройство {} ({})",
        "Connected to watchdog {} {}, firmware {}" => "Подключён Watchdog {} {}, прошивка {}",
        "Watchdog {} rejects commands" => "Watchdog {} не принимает команды",
        "write failed" => "ошибка записи",
        "Lost connection to watchdog {}: {}" => "Потеряна связь с Watchdog {}: {}",
        "Watchdog timer suspended" => "Таймер Watchdog остановлен",
        "Watchdog timer resumed" => "Таймер Watchdog запущен",
        "Watchdog timer suspended before exit" => "Таймер Watchdog остановлен перед выходом",
        "The watchdog timer could not be suspended" => "Не удалось остановить таймер Watchdog",
        "The watchdog timer could not be suspended before exit" => {
            "Не удалось остановить таймер Watchdog перед выходом"
        }
        "Settings reset: the file could not be read and was kept as {}" => {
            "Настройки сброшены: файл не читается, он сохранён как {}"
        }
        "Settings reset: the file could not be read" => "Настройки сброшены: файл не читается",
        "Reset requested" => "Отправлена команда «Перезагрузка»",
        "Hard reset requested" => "Отправлена команда «Жёсткая перезагрузка»",
        "Shut down requested" => "Отправлена команда «Выключение ПК»",
        "Self-test requested" => "Отправлена команда «Самотест»",
        "The watchdog is not connected" => "Watchdog не подключён",
        "The command did not reach the watchdog" => "Команда не дошла до устройства",
        "Web server started at {}" => "Веб-сервер запущен: {}",
        "Cannot start the web server on port {}: {}" => {
            "Не удалось запустить веб-сервер на порту {}: {}"
        }

        // Web page
        "Pro2" => "Pro2",
        "Lite" => "Lite",
        "Confirm" => "Подтверждение",
        "Power-cycle the PC now? Unsaved work is lost." => {
            "Жёстко перезагрузить ПК сейчас? Несохранённые данные пропадут."
        }
        "Press the reset button of the PC now?" => "Нажать кнопку Reset ПК сейчас?",
        "Heartbeat sent" => "Сигнал отправлен",
        "connected" => "подключён",
        "Type" => "Тип",
        "State" => "Состояние",
        "Last update" => "Обновлено",
        "n/a" => "н/д",
        "closed" => "закрыт",
        "open" => "открыт",
        "Uptime" => "Время работы",
        "Update" => "Обновить",
        "Refresh" => "Обновить",
        _ => return None,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn placeholders_survive_translation() {
        // Every Russian template keeps the English one's `{}` count.
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
        assert_eq!(trf("CH{} closed", &[&2]), "CH2 closed");
        assert_eq!(trf("{} and {}", &[&1]), "1 and {}");
    }
}
