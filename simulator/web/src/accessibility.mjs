const CONNECTIONS = ['Сопряжение', 'Подключено', 'Нет связи', 'Ошибка соединения'];

export function describeSnapshot(snapshot) {
  const parts = [`${CONNECTIONS[snapshot.connection] ?? CONNECTIONS[3]}.`];
  parts.push(snapshot.batteryPercent > 100 ? 'Заряд неизвестен.' :
    `Батарея ${snapshot.batteryPercent}%.`);
  parts.push(snapshot.total > 0 ?
    `Уведомление ${snapshot.position} из ${snapshot.total}.` : 'Уведомлений нет.');
  parts.push(snapshot.displayAwake ? 'Экран включён.' : 'Экран выключен.');
  if (snapshot.notificationPresent) {
    const title = snapshot.notification.title.trim();
    const message = snapshot.notification.message.trim();
    if (title) parts.push(`${title}.`);
    if (message) parts.push(`${message}.`);
    if (!title && !message) parts.push('Содержимое скрыто.');
  }
  return parts.join(' ');
}

export function createLiveRegionUpdater(target) {
  let previous = null;
  return (snapshot) => {
    const description = describeSnapshot(snapshot);
    if (description === previous) return false;
    target.textContent = description;
    previous = description;
    return true;
  };
}
