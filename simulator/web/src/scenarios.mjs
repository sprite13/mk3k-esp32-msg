export const UI_SCENARIOS = Object.freeze({
  normal: Object.freeze({ label: 'Обычное сообщение', actions: [
    { type: 'previews', hidden: false }, { type: 'connect', connection: 1 },
    { type: 'notify', title: 'Алиса', subtitle: 'Семья', message: 'Буду через 10 минут' },
  ] }),
  'title-only': Object.freeze({ label: 'Только имя', actions: [
    { type: 'previews', hidden: false }, { type: 'connect', connection: 1 },
    { type: 'notify', title: 'Михаил', subtitle: '', message: '' },
  ] }),
  'message-only': Object.freeze({ label: 'Только текст', actions: [
    { type: 'previews', hidden: false }, { type: 'connect', connection: 1 },
    { type: 'notify', title: '', subtitle: '', message: 'Проверь, пожалуйста, сборку' },
  ] }),
  empty: Object.freeze({ label: 'Пустое уведомление', actions: [
    { type: 'previews', hidden: false }, { type: 'connect', connection: 1 },
    { type: 'notify', title: '', subtitle: '', message: '' },
  ] }),
  long: Object.freeze({ label: 'Длинный текст', actions: [
    { type: 'previews', hidden: false }, { type: 'connect', connection: 1 },
    { type: 'notify', title: 'Команда', subtitle: 'Проект', message: 'Это длинное сообщение проверяет перенос строк и многоточие тем же C-кодом, что использует прошивка устройства.' },
  ] }),
  'six-message-burst': Object.freeze({ label: 'Серия из 6', actions: [
    { type: 'previews', hidden: false }, { type: 'connect', connection: 1 },
    ...Array.from({ length: 6 }, (_, index) => ({ type: 'notify',
      title: `Сообщение ${index + 1}`, subtitle: '', message: `Текст ${index + 1}` })),
  ] }),
  'connection-loss-recovery': Object.freeze({ label: 'Потеря связи', actions: [
    { type: 'connect', connection: 2 }, { type: 'wait', milliseconds: 700 },
    { type: 'connect', connection: 1 },
  ] }),
  'hidden-previews': Object.freeze({ label: 'Скрытые previews', actions: [
    { type: 'connect', connection: 1 }, { type: 'previews', hidden: true },
    { type: 'notify', title: 'Секрет', subtitle: 'Чат', message: 'Скрытый текст' },
  ] }),
});
