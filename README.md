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

Сейчас проект содержит только собираемый каркас: `app_main` выводит сообщение о загрузке. В конфигурации включены NimBLE в ролях central/observer, одно BLE-соединение, сохранение bonding-ключей в NVS, защищённое pairing, поддержка управления питанием и предупреждения о записи в строковые литералы.

Поддержка ANCS, Telegram, дисплея, кнопок и аппаратных периферийных устройств пока не реализована. Сборка на ESP-IDF проверяется программно; работа на реальном M5StickC Plus2 не проверялась.
