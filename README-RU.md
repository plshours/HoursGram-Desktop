# HoursGram Desktop

HoursGram Desktop — нативный Telegram-клиент на C++/Qt, основанный на AyuGram Desktop 7.0.9 и Telegram Desktop. Приложение и Windows-сборка оформлены под названием HoursGram.

## Сборка для Windows

В репозитории настроен GitHub Actions: он собирает x64-клиент и прикладывает результат как артефакт запуска workflow. Открой вкладку **Actions**, выбери завершённый запуск `HoursGram Desktop Windows Build` и скачай артефакт `HoursGram-Windows-x64`. Автоматической публикации в GitHub Releases пока нет.

Для локальной сборки следуй [руководству по Windows](docs/building-win.md).

```powershell
git clone --recursive https://github.com/sixseven67123/HoursGram-Desktop.git hoursgram
```

## Исходный код и авторство

HoursGram — неофициальный Telegram-клиент. В репозитории сохранены исходные лицензии и уведомления об авторстве AyuGram Desktop и Telegram Desktop. См. [LICENSE](LICENSE), [AyuGram Desktop](https://github.com/AyuGram/AyuGramDesktop) и [Telegram Desktop](https://github.com/telegramdesktop/tdesktop).
