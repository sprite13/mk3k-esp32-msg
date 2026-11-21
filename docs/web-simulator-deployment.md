# Публикация WebAssembly preview

Preview является набором статических файлов. Он не требует backend, базы данных
или секретов и публикуется только по адресу
`https://les13.tech/mk3k-esp32-preview/`.

## Локальная сборка и пакет

```bash
scripts/install-emsdk.sh
scripts/package-web-simulator.sh
scripts/package-web-simulator.sh --check artifacts/mk3k-esp32-preview
(cd artifacts/mk3k-esp32-preview && sha256sum -c SHA256SUMS)
```

Упаковщик сначала запускает закреплённую WebAssembly-сборку. Затем он создаёт
`artifacts/mk3k-esp32-preview` через временный каталог и нормализует права:
каталоги `0755`, файлы `0644`. В пакет входят только `index.html`, runtime
ES-модули, CSS, SVG и файлы `.mjs`/`.wasm` из `dist`. Тесты, исходный C,
нативные артефакты, source maps, dotfiles и символические ссылки запрещены.
`SHA256SUMS` покрывает каждый runtime-файл ровно один раз.

## Предварительная проверка сервера

Перед первым деплоем необходимо в read-only режиме определить фактически
активный TLS `server` для `les13.tech`:

```bash
ssh clawd@les13.tech 'hostname; sudo nginx -T; df -h /var/www'
ssh clawd@les13.tech \
  'sudo find /var/www/mk3k-esp32-preview -maxdepth 2 -type d -o -type l 2>/dev/null || true'
```

На момент проектирования основным активным файлом был обычный файл
`/etc/nginx/sites-enabled/les13.tech.conf`. В `sites-enabled` также находилась
старая backup-ссылка `les13.tech.conf.bak-20261110T1042Z`, из-за которой nginx
уже сообщал предупреждения о конфликтующем `server_name`, хотя `nginx -t`
проходил. Деплой preview не должен изменять или удалять эту ссылку и не должен
«чинить» постороннюю конфигурацию.

Перед изменением nginx:

1. прочитать `sudo nginx -T` и найти реальный HTTPS-блок `les13.tech`;
2. сохранить отдельную датированную копию именно изменяемого файла;
3. проверить, что маршрутов `/mk3k-esp32-preview*` ещё нет;
4. добавить содержимое `deploy/nginx/mk3k-esp32-preview.conf` внутрь
   существующего TLS `server`, не заменяя `/`, `/tasks`, `/athar-data` и другие
   location;
5. выполнить `sudo nginx -t` и только затем `sudo systemctl reload nginx`.

Фрагмент явно отключает inherited Basic Auth для preview, запрещает listing,
задаёт MIME для `.wasm` и остальных runtime-файлов, а также возвращает
`X-Robots-Tag`, `Cache-Control: no-cache` и `nosniff`. Redirect без завершающего
слеша оставляет относительные URL корректными.

## Деплой release

Пользователь SSH всегда фиксирован как `clawd`; пароль, токен или ключ не
передаются аргументами и не выводятся скриптом. Обычный запуск:

```bash
scripts/deploy-web-simulator.sh \
  --host les13.tech \
  artifacts/mk3k-esp32-preview
```

Необязательный идентификатор удобно использовать при повторяемом релизе:

```bash
scripts/deploy-web-simulator.sh \
  --host les13.tech \
  --release 20261121T120000Z-abcdef123456 \
  artifacts/mk3k-esp32-preview
```

Без `--release` скрипт формирует идентификатор из UTC-времени и короткого SHA
manifest. Допускаются только буквы, цифры, `.`, `_` и `-`; host также проходит
строгую проверку. Переопределение `MK3K_REMOTE_BASE` предназначено для
изолированных тестов и аварийной эксплуатации, обычный путь —
`/var/www/mk3k-esp32-preview`.

Порядок операции:

1. локальный пакет полностью проверяется;
2. на сервере создаётся уникальный `.incoming-<release>`;
3. tar-поток передаётся по SSH, затем `SHA256SUMS` проверяется на сервере;
4. release перемещается в `releases/<release>` и становится read-only;
5. относительные ссылки `current` и `previous` переключаются через временные
   ссылки и rename;
6. выполняются `nginx -t` и reload.

Существующий release никогда не перезаписывается. Старые releases автоматически
не удаляются. Если validation или reload завершается ошибкой, ссылки
`current`/`previous` восстанавливаются; новый неизменяемый release остаётся для
диагностики.

## Rollback

```bash
scripts/deploy-web-simulator.sh --host les13.tech --rollback
```

Rollback меняет `current` и `previous` местами, проверяет nginx и делает reload.
Поэтому повторный rollback возвращает предыдущую пару. Небезопасные, абсолютные
или отсутствующие targets отвергаются. При ошибке проверки/reload исходная пара
ссылок восстанавливается.

Проверка после публикации:

```bash
curl -fsSI https://les13.tech/mk3k-esp32-preview/
curl -fsSI https://les13.tech/mk3k-esp32-preview/dist/mk3k-simulator.wasm
curl -fsS https://les13.tech/mk3k-esp32-preview/ \
  | rg 'noindex|device-screen|notification-form'
```

Ожидаются `200`, `Content-Type: application/wasm`, заголовок
`X-Robots-Tag: noindex, nofollow` и HTML-маркеры интерфейса.

## Проверенный публичный деплой 2026-11-21

- URL: `https://les13.tech/mk3k-esp32-preview/`.
- Активный release: `20261121T114500Z-0618b23d845a`.
- Предыдущий release: `20261121T114303Z-0618b23d845a`.
- SHA256 опубликованного WASM:
  `f0833f309536e488020e15e9b6f16b2caa1981e634eed7efe331fad084cc2b21`.
- Исходный nginx-файл сохранён вне `sites-enabled` в
  `/var/backups/nginx/les13.tech.conf.20261121T1144Z.bak`.
- `nginx -t` и reload прошли. Старое предупреждение о конфликтующем
  `server_name` осталось без изменений; посторонняя backup-ссылка не менялась.
- Проверены redirect `301`, HTML `200`, WASM `200` с
  `Content-Type: application/wasm`, а также `noindex`, `no-cache` и `nosniff`.
- В браузере проверены кириллическое уведомление, очередь из пяти после серии
  из шести, навигация A/B, состояние «Нет связи» и выключение экрана после
  реальных 10 секунд.
- В мобильном viewport 390 px нет горизонтального overflow; интерактивные
  кнопки A/B и основные поля имеют высоту не менее 44 px.
- Выполнен rollback со второго release на первый, проверен тот же SHA256, затем
  повторным rollback восстановлен второй release. Пара `current`/`previous`
  осталась готова к следующему аварийному откату.
