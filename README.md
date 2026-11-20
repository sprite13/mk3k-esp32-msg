# M5StickC Plus2 Telegram ANCS

Минимальный каркас прошивки для M5StickC Plus2 на чистом ESP-IDF. Проект закреплён на ESP-IDF v6.1.1 и рассчитан на целевую платформу ESP32. Arduino и M5Unified не используются.

## Установка toolchain

ESP-IDF устанавливается локально в домашний каталог без системных изменений:

```bash
mkdir -p "$HOME/esp"
git clone --depth 1 --branch v6.1.1 --recursive \
  https://github.com/espressif/esp-idf.git \
  "$HOME/esp/esp-idf-v6.1.1"
"$HOME/esp/esp-idf-v6.1.1/install.sh" esp32
```

Если каталог уже существует, повторно клонировать репозиторий не нужно. Проверьте закреплённую версию:

```bash
git -C "$HOME/esp/esp-idf-v6.1.1" describe --tags --exact-match
```

Команда должна вывести `v6.1.1`.

## Сборка

Из корня рабочего дерева выполните:

```bash
source "$HOME/esp/esp-idf-v6.1.1/export.sh"
cd repos/m5stickc-telegram-ancs
idf.py set-target esp32
idf.py build
```

Готовый образ приложения будет создан в `build/m5stickc_telegram_ancs.bin`.

## Текущее состояние

Сейчас проект содержит только собираемый каркас: `app_main` выводит сообщение о загрузке. В `sdkconfig.defaults` включены NimBLE в ролях central и observer, одно соединение, хранение bonding-данных в NVS, legacy и Secure Connections pairing, управление питанием и предупреждения о записи в строковые литералы.

Сборка использует поддерживаемый ESP-IDF режим `MINIMAL_BUILD`, а компонент `main` явно зависит от официального компонента `bt`. В ESP-IDF v6.1.1 bonding включён самим NimBLE port константой `MYNEWT_VAL_BLE_SM_BONDING=1`; отдельного Kconfig-символа `CONFIG_BT_NIMBLE_SM_BONDING` в этой версии нет.

На ESP32 официальный компонент `bt` транзитивно выбирает официальный `esp_wifi`, поэтому код Wi-Fi может входить в образ даже при `MINIMAL_BUILD`. Скрытый `CONFIG_ESP_WIFI_ENABLED` в ESP-IDF v6.1.1 нельзя надёжно выключить через `sdkconfig.defaults` для ESP32. Каркас не вызывает инициализацию или запуск Wi-Fi, поэтому Wi-Fi-радио остаётся выключенным во время выполнения. Локальных подмен компонентов ESP-IDF нет; прошивка не использует Wi-Fi и не передаёт уведомления наружу.

Поддержка ANCS, Telegram, дисплея, кнопок и аппаратных периферийных устройств пока не реализована. Сборка на ESP-IDF проверяется программно; работа на реальном M5StickC Plus2 не проверялась.
