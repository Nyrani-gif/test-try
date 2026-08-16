# Sanae — Aegisub v0.3 (beta): UX/client handoff

## Исходная база

Работа выполнена поверх переданного архива `Aegisub-Sanae-beta-02-source.rar` (SHA-256 `09cdf21bc77ee4b67df47bcc4035df7266316b5fc66dad10270d5caafd3081da`, embedded git HEAD `4eb51be8ee8249b4682d22f3f590014d153164bb`). Backend не изменялся.

## 1. Что было плохо в исходном UI

Final Review был горизонтальным notebook из технически равноправных вкладок, включая отдельную вкладку Finalize. Списки кандидатов показывали статистику как debug-текст, не давали нормального Windows multiselect и часто оставляли пустые белые области. Кандидатная логика генерировала все 1–4-граммы без надёжной фильтрации обычных английских слов. Internal Consistency считала вариативность коротких бытовых реплик проблемой. Source Repeat сравнивал текст так, что HTML-подобная разметка `<i>...</i>` ломала Exact.

## 2. Как перестроен Final Review

Final Review теперь — category browser: категории слева, список результатов и контекст справа, постоянный summary и кнопки `Закрыть` / `Финализировать серию` снизу. Отдельной вкладки Finalize больше нет. Окно resizable; списки и контекст разделены splitter-ом.

## 3. Категории

- Кандидаты
- Терминология
- Повторы исходных реплик
- Согласованность
- Подготовленные термины
- Исключения

Summary различает замечания, рекомендации и информационные результаты. Exact-повтор с уже совпадающим переводом считается информацией, а не незакрытой рекомендацией.

## 4. Multiselect

Review-списки переведены на native `wxListCtrl`/`wxListBox` с extended selection. Работают Ctrl+Click, Shift+Click и Ctrl+A. Массовые действия применяются ко всему selection. Изменения открытого ASS коммитятся одним `AssFile::Commit`, поэтому Ctrl+Z откатывает пакет целиком.

## 5. Фильтрация terminology candidates

Кандидаты строятся по 1–4-граммам, но затем проходят сильную фильтрацию: существующая терминология/черновики/исключения отбрасываются, обычные словарные single-word кандидаты не показываются, редкие неизвестные слова и устойчивые multiword/name-like фразы получают приоритет. Перекрывающиеся n-граммы схлопываются в максимальную не-дублирующую фразу, когда статистика occurrences совпадает.

## 6. English dictionary

Используется существующий `SpellCheckerFactory`/Hunspell Aegisub. Добавлена возможность создать spellchecker для явно заданного языка без изменения пользовательской настройки. Sanae предпочитает установленный `en_US`, затем `en_GB`, затем другой `en*`. Hunspell по-прежнему загружает штатный user dictionary, поэтому пользовательские слова также не навязываются как кандидаты.

## 7. Common-word/noise rules

При наличии English Hunspell обычные dictionary words удаляются автоматически. В качестве fallback для установок без English dictionary расширен консервативный stopword-набор, включающий в том числе `like`, `after`, `why`, `time`, `all`, `well`, `day`, `get`, `come`, `one`, `know`, `would`, `should`, `something`, `someone`.

## 8. Internal Consistency

Короткие common utterances (`yes/no/yeah/what/why/thanks/okay/...`) и малоинформативные короткие источники не создают предупреждений о разных переводах. Остаются более содержательные повторяющиеся source lines и консервативная проверка единичного spelling-варианта против формы, встречающейся не менее пяти раз.

## 9. Exact Source Repeat

Добавлена отдельная repeat-normalization: удаляются ASS override blocks и presentation-only `<i>`, `<b>`, `<u>`, `<s>`, `\N`/`\n` превращаются в пробелы, затем выполняется NFKC/whitespace/case normalization. RUSUB не участвует в ключе обнаружения повторов.

## 10. Fragment Repeat

Добавлен `SanaeRepeatKind::Fragment`. Для поиска используются индексированные окна из 6 токенов, после чего совпадение подтверждается консервативно: continuous run минимум 6 токенов, покрытие минимум 75% более короткой строки и минимум 20 значимых символов. Fragment — reference-only и не разрешает автоматический перенос русского перевода.

## 11. Ctrl+F

При отсутствии Sanae Project обычный Find не меняется. При активном проекте в Ctrl+F появляется `Текущий файл / Весь проект`. Whole Project использует существующий `SanaeProjectManager::SearchMemory`, а не второй search engine; доступны RU/EN/оба, fuzzy word forms и фильтр серии. Поиск теперь также включает текущий открытый ASS. Double-click по результату текущей серии переходит к строке; другой эпизод не переключается разрушительно автоматически.

## 12. Другие Sanae dialogs

- отдельный `Possible new terms` переведён на таблицу с reason/context и multiselect;
- Terminology скрывает raw term versions и получает empty states;
- Episode Details больше не показывает SHA-256 обычному пользователю и имеет empty states для файлов/финализированных версий;
- действие, которое только снимает Sanae binding с текущего ASS, переименовано из неоднозначного `Close project` в `Detach current episode`;
- существующие Project Browser/connection/batch import проверены: их текущая структура уже существенно ближе к нормальному пользовательскому UI, поэтому они не переписывались ради переписывания.

## 13. Русификация

Добавлены русские строки нового Final Review, Ctrl+F Project mode, empty states, candidate reasons и новых действий. Исправлена ошибочная старая локализация `Correct selected occurrences`, которая была переведена как сортировка; теперь `Correct selected`/старое сообщение переводятся как `Исправить выбранные`. Аудит текущих Sanae UI gettext-строк: отсутствующих msgid в `ru.po` не осталось.

## 14. Regression tests

В `tests/tests/sanae_text.cpp` добавлены тесты:

- plain text vs `<i>same text</i>` => одинаковый repeat key;
- `\N` vs equivalent whitespace => одинаковый repeat key;
- реальный длинный flashback fragment => Fragment;
- короткие `I think`/4-word phrase => не Fragment;
- fixture документирует, что RUSUB style `Default` vs `Default – Flashback`, timing +10 ms и RU comment не входят в ENSUB repeat key.

## 15. Build/test results

В доступном Linux-контейнере выполнено:

- `git diff --check` — PASS;
- direct C++20 smoke build `sanae_text.cpp` + Boost.Locale/ICU — PASS;
- production strings: `<i>` Exact, `\N` normalization, Fragment — PASS;
- `tools/check-sanae-v02-contract.py` против приложенного OpenAPI v0.2 — PASS;
- `python3 -m py_compile tools/check-sanae-v02-contract.py` — PASS;
- `tools/version.sh` — выдаёт `Sanae — Aegisub v0.3 (beta)`, beta 03;
- gettext audit по текущим Sanae dialogs/search — 0 отсутствующих msgid.

Полный Meson/Ninja + Windows/MSVC/wxWidgets build здесь выполнить невозможно: Meson и wxWidgets dev environment отсутствуют, сеть для установки зависимостей недоступна. Обязательная следующая проверка — штатный Windows CI/MSVC build и ручной GUI smoke.

## 16. Сознательно оставшиеся UX-вопросы

- Для terminology mismatch не добавлено обещающее persistence действие `Игнорировать`: текущий server v0.2 имеет ignore operations для candidate text/scope, но не отдельную модель suppression конкретного terminology occurrence. Backend менять было запрещено, поэтому не создано вводящее в заблуждение псевдо-игнорирование.
- Результат поиска другой серии не переключает рабочий ASS автоматически. Это намеренно безопасно; отдельный flow открытия другого рабочего RUSUB требует ясной политики сохранения/выбора локальной рабочей копии и не должен притворяться простой навигацией.
- Полный Windows visual pass (DPI, реальные размеры колонок, системная тема) должен быть подтверждён на целевой сборке.

## Примеры «было → стало»

Было: `like — episodes: 5, current: 11, previous: 54 ...`

Стало: обычное `like` отфильтровывается English dictionary. Для полезного кандидата показывается таблица `Кандидат / Серий / В этой серии / Раньше / Причина`, а ниже — EN/RU контекст по сериям и времени.

Было: `Correct selected occurrences`

Стало: `Исправить выбранные`.

Было: `<i>You're just using your age as an excuse\Nto tell yourself that, aren't you?</i>` не совпадало с plain-text прошлой строкой.

Стало: `Точное совпадение`; presentation markup не участвует в source key.

Было: `This is about as far as I go.` терялось на фоне более длинной прошлой строки.

Стало: `Фрагмент прошлой реплики`; прошлый EN/RU показывается как reference без auto-translation.
