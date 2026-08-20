# Sanae / Aegisub — Технический проект ревампа UX/UI, QC и производительности

**Версия документа:** 2.1.1-final-sync (заморожен; клиентская архитектура/UX/фазы. Authoritative для серверного wire/API — `SANAE_SERVER_REQUIREMENTS_v0.3.md`)
**Анализируемая кодовая база:** `Aegisub-Sanae-beta-02-source` (C++/wxWidgets)
**Анализируемый сервер:** `sanae-server-openapi-v0.2.json` (22 эндпоинта, 15 доменных сущностей)
**Принцип:** «Показывать нужную функцию тогда, когда она нужна, а не показывать все возможности программы одновременно». Простота без потери мощности. Машина вычисляет то, что действительно может вычислить; человек принимает смысловые решения; интерфейс показывает только актуальную информацию.

---

## 0. Краткое резюме

Текущий Sanae-форк функционально завершён, но его UX организован вокруг **диалоговых окон**, а не вокруг **потока перевода**. Переводчик вынужден держать термины в голове, открывать модальные окна для терминологии/QC/поиска, вручную проставлять статусы и финализировать эпизод синхронно в UI-потоке.

**Сервер v0.2** хранит файлы как непрозрачные блобы и глоссарий, не имеет per-line сущности и не имеет workflow-состояния эпизода кроме `translating/finalized/archived`. Это блокирует multi-device QC.

**План Rev 2.0:**

1. **Разделить модели хранения** диагностики (transient, computed) и замечаний (persistent, human). Объединить только на уровне отображения.
2. **Inline-терминология** через Aho-Corasick: тяжёлый поиск по EN — один раз на смену строки, лёгкая проверка usage по RU — на каждое нажатие без debounce. **Без auto-misused** до появления aliases/morphology.
3. **Единая state machine** `open / ready_for_review / resolved / wont_fix` с `modified_after_issue` как вычисляемым флагом (не состоянием), синхронизируемым через baseline fingerprints.
4. **Серверный episode review workflow**: `review_state` поле на `EpisodeBody` + endpoint для переходов. Решает multi-device проблему «как ПК B узнает, что серия на проверке».
5. **QC-профили с пресетами** (Стандарт команды / Строгий / Минимальный / Пользовательский). 95% переводчиков не видят параметров.
6. **Одна панель «Контекст строки»** вместо стека панелей, с workspace-sensitive actions.
7. **Phase 0 — instrumentation + UX-baseline** до любых оптимизаций.
8. **`line_ref` — design spike**, не принятое решение.

Документ организован: анализ проблем → новая архитектура → детали по направлениям → сервер → фазы → KPI. Changelog — в конце, кратко.

---

## 1. Методология и источники

Анализ выполнен по 4 осям (подробные отчёты в `/home/z/my-project/worklog.md`):

| Подзадача | Что исследовалось | Ключевые файлы |
|---|---|---|
| A — Терминология | UI, алгоритмы, производительность, локализация | `dialog_sanae_terminology.cpp`, `sanae_project.cpp:2661-3134`, `sanae_text.cpp`, `command/sanae.cpp`, `subs_edit_box.cpp` |
| B — QC и Final Review | Модель данных, состояния, recovery, диалоги | `dialog_sanae_final_review.cpp`, `sanae_recovery.cpp`, `sanae_subtitle_diff.cpp`, `dialog_sanae_episode.cpp`, `translation_project.h/.cpp` |
| C — Главный экран и перформанс | Компоновка, виртуализация, цепочки обновлений | `frame_main.cpp`, `base_grid.cpp`, `subs_edit_box.cpp`, `dialog_translation.cpp`, `project.cpp` |
| D — Серверный OpenAPI | Все эндпоинты, схемы, лимиты | `sanae-server-openapi-v0.2.json` (4455 строк) |

Все цитаты кода — с указанием `файл:строка` относительно корня `/home/z/my-project/upload/sanae-src/`.

---

## 2. Реальные UX-проблемы текущей реализации

### 2.1. Два несвязанных QC-слоя

В Sanae есть **два независимых QC-слоя**, которые ничего не знают друг о друге:

| Слой | Владелец | Хранилище | Где виден в UI |
|---|---|---|---|
| **A. Per-line `QCIssue` + `ReviewStatus`** | `TranslationProject` (`translation_project.h:54, 23`) | JSON-сайдкар `<file>.ass.aegisub.json` | Колонки сетки «Review» и «QC», футер Translation Assistant |
| **B. `SanaeReviewIssue`** (терминология/консистентность/повторы) | `SanaeProjectManager` (`sanae_project.h:183`) | Сервер + локальный черновик | Модальное окно `FinalReviewDialog` |

`translation_project.h:54-59`:
```cpp
struct QCIssue {
    enum class Severity { Warning, Error };
    Severity severity = Severity::Warning;
    std::string code;     // "duration","cps","length","spaces","tags","style","overlap"
    std::string message;
};
```

`sanae_project.h:183-189`:
```cpp
struct SanaeReviewIssue {
    std::string title;
    std::string detail;
    AssDialogue *line = nullptr;
    std::string replacement_from;
    std::string replacement_to;
};
```

Ни у одной нет `id`, `state`, `author`, `comment`. `FinalReviewDialog` не вызывает `CheckLine()`. Переводчик видит автопроверки только в колонке сетки, а семантические проблемы — только в модальном окне.

### 2.2. Терминология невидима во время перевода

`subs_edit_box.cpp:442-449`:
```cpp
void SubsEditBox::OnChange(wxStyledTextEvent &event) {
    if (line && edit_ctrl->GetTextRaw().data() != line->Text.get()) {
        if (event.GetModificationType() & wxSTC_STARTACTION)
            commit_id = -1;
        CommitText(_("modify text"));
        UpdateCharacterCount(line->Text);
    }
}
```

При изменении текста — только `CommitText` и счётчик символов. Никакого терминологического поиска. Единственный calltip (`subs_edit_ctrl.cpp:312-333`) — это `agi::GetCalltip` для ASS-тегов.

Терминология доступна только через модальные `TerminologyDialog` (`dialog_sanae_terminology.cpp:545`), `TerminologyEntryDialog` (`dialog_sanae_final_review.cpp:39`) или `FinalReviewDialog`.

### 2.3. Терминологический поиск — O(T·(M+N)) без кэша

`sanae_project.cpp:2865-2945` (`TerminologyConsistencyIssues`):
```cpp
for (auto const& term : active_terms) {
    ...
    for (auto const& entry : memory) {
        if (!contains_normalized(entry.source, term.english)) continue;
        ...
    }
    for (auto& line : context->ass->Events) {
        auto source = context->translationProject->SourceDisplayTextCached(&line);
        if (!contains_normalized(source, term.english)) continue;
        ...
    }
}
```

Помощник `contains_normalized` (`sanae_project.cpp:505-507`):
```cpp
bool contains_normalized(std::string const& text, std::string const& needle) {
    return SanaeNormalizeSource(text).find(SanaeNormalizeSource(needle)) != std::string::npos;
}
```

`SanaeNormalizeSource` делает две ICU-операции (NFKC + fold_case) на каждый вызов. Для 25 серий × 300 строк × 200 терминов — ~1.56 млн substring-поисков и ~3.13 млн ICU-нормализаций на один `Populate()`. `Populate()` синхронно в UI-потоке, вызывается при открытии `FinalReviewDialog` и после любого действия пользователя.

`GenerateCandidates` (`sanae_project.cpp:2661-2863`): O((M+N)·W·4·|norm|) на n-граммную экстракцию, O(R²) на дедуп, плюс полная перезагрузка Hunspell на каждый вызов (`sanae_project.cpp:2745`).

### 2.4. Flat vector и linear scans для терминологии

`sanae_project.h:238-242`:
```cpp
std::vector<SanaeTerminologyEntry> terminology;
std::vector<SanaeTerminologyHistoryEntry> terminology_history;
std::vector<SanaeIgnoredCandidate> ignored_candidates;
std::vector<SanaeTerminologyDraft> terminology_drafts;
std::vector<SanaeIgnoreDraft> ignore_drafts;
```

Все операции `QueueTerminology*` (`sanae_project.cpp:3023-3112`) — `std::find_if` по всему вектору. Поле `english_normalized` (`sanae_project.h:97`) существует и сервер заполняет, но Queue* операции его игнорируют и пересчитывают `SanaeNormalizeSource(term.english)`.

### 2.5. Финальный ревью — модальное 6-страничное окно без навигации

`dialog_sanae_final_review.cpp:671-813`:
```cpp
FinalReviewDialog(...)
: wxDialog(parent ? parent : c->parent, -1, _("Final review"), wxDefaultPosition,
           wxSize(1080, 720), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    auto categories = new wxListBox(...);  // 220px левая панель
    auto book = new wxSimplebook(...);     // 6 страниц, без табов
}
```

6 страниц: Candidates / Terminology / Source repeats / Consistency / Prepared terms / Exclusions. Единственная горячая клавиша — `Ctrl+A` в списках. Нет «следующее замечание», нет Enter-to-jump, нет batch-accept.

«Go to line» вызывает `SetSelectionAndActive({line}, line)`, но диалог остаётся модальным — пользователь не может редактировать строку под ним.

`manager.Finalize()` (`sanae_project.cpp:3186-3288`) синхронно в UI-потоке: compact RUSUB + SHA-256 + POST + JSON parse + `RebuildMemory()` (перепарсит все исторические ASS) + `RebuildRepeatCache()` (O(N·M)). `wxBusyCursor` отсутствует.

### 2.6. Ручные статусы там, где состояние выводится из действий

`translation_project.h:23-34`:
```cpp
enum class ReviewStatus {
    Untranslated, Draft, NeedsContext, OCRDoubt,
    MeaningChecked, Polished, Typeset, QCPassed, Final, Count
};
```

Хранится в сайдкаре (`translation_project.cpp:1125`). Меняется вручную через `SetStatus`/`AdvanceStatus` (`translation_project.cpp:562-603`). Единственный авто-переход: `Untranslated → Draft` при вводе.

Нет связи между `ReviewStatus` и `QCIssue`: линия может быть `QCPassed`, имея `Error`-severity `QCIssue`. `FinalReviewDialog` не читает `ReviewStatus`.

### 2.7. Фиксированная компоновка `frame_main`, без режимов

`frame_main.cpp:187-219`:
```cpp
ToolsSizer = new wxBoxSizer(wxVERTICAL);
ToolsSizer->Add(audioBox, 0, wxEXPAND);
ToolsSizer->Add(EditBox, 1, wxEXPAND);
TopSizer = new wxBoxSizer(wxHORIZONTAL);
TopSizer->Add(videoBox,0, wxEXPEND, 0);
TopSizer->Add(ToolsSizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
MainSizer = new wxBoxSizer(wxVERTICAL);
MainSizer->Add(new wxStaticLine(Panel),0,wxEXPAND,0);
MainSizer->Add(TopSizer,0,wxEXPAND,0);
MainSizer->Add(context->subsGrid,1,wxEXPAND,0);
```

Grep по `wxAuiManager|wxAuiNotebook|wxAUI` → 0 совпадений. Нельзя расширить сетку, нельзя докинуть панель терминов сбоку, нельзя скрыть edit box. В `frame_main.cpp` нет понятия «режим перевода/QC/расширенный».

### 2.8. Edit box: только перевод, без исходного текста рядом

`subs_edit_box.cpp:200-208`:
```cpp
edit_ctrl = new SubsTextEditCtrl(...);
secondary_editor = new wxTextCtrl(this, -1, "", ...);
main_sizer->Add(secondary_editor,1,wxEXPAND | ...);
main_sizer->Add(edit_ctrl,1,wxEXPAND | ...);
main_sizer->Hide(secondary_editor);
```

`secondary_editor` скрыт по умолчанию. Когда показан через `split_box`, он показывает `c->initialLineState->GetInitialText()` — **начальное состояние текущей строки**, а не EN source. Источник виден только в отдельной колонке сетки или в модальном Translation Assistant.

### 2.9. Modal-диалоговая усталость: 16 wxDialog-классов

| # | Класс | Файл:строка | Заменяется инлайн? |
|---|---|---|---|
| 1 | `TerminologyEntryDialog` | `dialog_sanae_final_review.cpp:39` | Да — popover |
| 2 | `FinalReviewDialog` | `dialog_sanae_final_review.cpp:149` | Да — док-панель |
| 3 | `SemanticDiffDialog` | `dialog_sanae_episode.cpp:118` | Да — боковая панель |
| 4 | `EpisodeDetailsDialog` | `dialog_sanae_episode.cpp:186` | Частично |
| 5 | `ConnectionDialog` | `dialog_sanae_connection.cpp:21` | Нет (одноразовая настройка) |
| 6 | `SanaeBatchImportDialog` | `dialog_sanae_batch_import.cpp:54` | Нет (wizard) |
| 7 | `CreateSeasonDialog` | `dialog_sanae_project.cpp:100` | Да — inline form |
| 8 | `CreateProjectDialog` | `dialog_sanae_project.cpp:165` | Да — inline form |
| 9 | `ProjectDialog` | `dialog_sanae_project.cpp:251` | Да — док-навигатор |
| 10 | `TermEditDialog` | `dialog_sanae_terminology.cpp:62` | Да — объединить с #1 |
| 11 | `TerminologyCandidateDialog` | `dialog_sanae_terminology.cpp:106` | Да — **дубликат** страницы Candidates в #2 |
| 12 | `TerminologyDialog` | `dialog_sanae_terminology.cpp:241` | Да — док-панель |
| 13 | `ProjectSearchDialog` | `command/sanae.cpp:40` | Да — панель поиска |
| 14 | `RepeatHistoryDialog` | `command/sanae.cpp:182` | Да — инлайн в LineContextPanel |
| 15 | `SanaeUpdateDialog` | `dialog_version_check.cpp:96` | Уже не модальный |
| 16 | `VersionCheckerResultDialog` | `dialog_version_check.cpp:147` | Оставить |

8 из 16 (#1, #3, #7, #8, #10, #11, #13, #14) — маленькие формы, заменяемые инлайн. `TerminologyCandidateDialog` (#11) — функционально **подмножество** страницы Candidates, отдельное существование — UX-дефект.

### 2.10. Локализационные баги

| Где | Ожидается | Фактически в `po/ru.po` |
|---|---|---|
| `dialog_sanae_terminology.cpp:201-205`, колонка `Occurrences` | «Вхождения» | **«Настройки»** (fuzzy, `ru.po:6912-6915`) |
| `dialog_sanae_terminology.cpp:481`, кнопка `Find new terms…` | «Найти новые термины…» | **«Добавить термин…»** (fuzzy, `ru.po:6985-6988`) |

Хрупкий паттерн (`dialog_sanae_final_review.cpp:80-88`):
```cpp
wxString issue_detail(std::string const& value) {
    auto result = to_wx(value);
    result.Replace("Accepted:", _("Accepted:"));
    result.Replace("In this line:", _("In this line:"));
    result.Replace("not detected", _("not detected"));
    result.Replace("Current episode:", _("Current episode:"));
    result.Replace("Project:", _("Project:"));
    return result;
}
```

Строки конкатенируются по-английски в `sanae_project.cpp:2934-2939`, потом `wxString::Replace`-ятся. Переводчик не может переставлять слова; любое изменение пунктуации ломает русский рендеринг.

### 2.11. Performance-хотспоты

| Где | Проблема | Файл:строка |
|---|---|---|
| `OnActiveLineChanged`, `OnSelectedSetChanged` | Всегда `Refresh(false)` (вся сетка) | `base_grid.cpp:278-293` |
| `SetColumnWidths` | Обходит все `c->ass->Events`; на каждый `COMMIT_DIAG_META` | `base_grid.cpp:755`, `grid_column.cpp:244-246` |
| `RebuildUnits` | На каждый `COMMIT_NEW/ORDER/DIAG_ADDREM` + триггерит `UpdateMaps` | `translation_project.cpp:250-265` |
| `GenerateCandidates` | O(R²) дедуп + полная загрузка Hunspell на каждый вызов | `sanae_project.cpp:2661-2863` |
| `TerminologyConsistencyIssues` | O(T·(M+N)) без кэша нормализованного текста | `sanae_project.cpp:2865-2945` |
| `manager.Finalize()` | Синхронно в UI-потоке: I/O + SHA + POST + парсинг + RebuildMemory + RebuildRepeatCache | `sanae_project.cpp:3186-3288` |
| `pending_finalize_key` | Сбрасывается любым `AssFile::COMMIT_DIAG_*` | `sanae_project.cpp:1885-1893` |

### 2.12. Сервер: нет per-line сущности и нет workflow-состояния

Из OpenAPI v0.2: 15 доменных сущностей. Только `EpisodeBody.status` (`["translating","finalized","archived"]`) несёт workflow-состояние, и оно целое-эпизодное.

- Нет `Issue`, `Comment`, `Review`, `Flag` сущностей.
- Нет per-line/per-event состояния.
- Нет episode review workflow (translating → in_review → needs_fixes → ...).
- Нет websocket/streaming/long-poll.
- Нет batch endpoint (только `terminology_ops[]` внутри `/finalize`, maxItems 1000).
- Нет серверного матчинга терминов — только полный словарь.

Это блокирует multi-device QC: два переводчика не видят замечания друг друга; ПК B не знает, что серия переведена в «In Review» на ПК A.

---

## 3. Новая архитектура пользовательского взаимодействия

### 3.1. Три режима интерфейса

В `FrameMain` вводится `WorkspaceMode { Translation, QC, Advanced }`, переключаемый `Ctrl+Shift+1/2/3` и кнопкой в тулбаре. Не три отдельных программы — три preset-а компоновки одного приложения.

| Режим | Что видит пользователь | Что скрыто по умолчанию |
|---|---|---|
| **Перевод** | Видео, аудио, текущая строка, соседние строки, LineContextPanel, мини-статус эпизода | Стили, ASS-теги, колонки Layer/Margin/Effect, панель терминов как отдельное окно, full QC-док |
| **QC** | Сетка, видео, QCIssueDock (слева), список проблем с фильтрами, режим «Следующая проблема», diff-просмотр | Терминология как отдельная сущность, панели тайминга, typesetting |
| **Расширенный** | Полный Aegisub: стили, ASS-теги, тайминг, karaoke, vector clip, все колонки, все диалоги | Ничего — текущий вид |

«Расширенный» режим всегда даёт доступ ко всем старым возможностям Aegisub (требование «простота без потери мощности»).

### 3.2. Главный экран переводчика + LineContextPanel

Главная новая UX-сущность — **одна** панель «Контекст строки» под edit box, с приоритетным содержимым. Не стек панелей (это воссоздавало бы проблему шума).

```
┌──────────────────────────────────────────────────────────────────────┐
│  [▶/⏸] [∣◀] [▶∣] Эп. 05 · Сезон 2026-1 · Project: "Series-XY"   [⚙] │
├────────────────────────────────┬─────────────────────────────────────┤
│                                │  EN: I'll definitely protect them!  │
│         ВИДЕО                  │  ─────────────────────────────────  │
│   [аудио-велосипед для строки] │  RU: [edit box — фокус ввода]       │
│                                │  ─────────────────────────────────  │
│                                │  ┌─ Контекст строки ──────────────┐ │
│                                │  │ ⚠ Замечание QC (open):         │ │
│                                │  │   «Сарказм не передан»         │ │
│                                │  │   [Готово к проверке] [Ответить]│ │
│                                │  │ 💬 protect → защищать  [Alt+1] │ │
│                                │  │ ⟳ Точный повтор из Ep.03 [Use] │ │
│                                │  └────────────────────────────────┘ │
├────────────────────────────────┴─────────────────────────────────────┤
│ #  │ Время │ EN (source)              │ RU (translation)         │ St│
│ 12 │ 00:42 │ I'll definitely protect  │ Я защищу их              │ ✓ │
│ 13 │ 00:45 │ What's wrong?            │ [фокус здесь]            │ ⚠ │
└──────────────────────────────────────────────────────────────────────┘
```

**Правила приоритета содержимого LineContextPanel:**

1. Критическое открытое `ReviewIssue` (severity=Error, state=Open) → показано развёрнутым.
2. Другие открытые `ReviewIssue` → компактные строки.
3. Релевантная терминология → top 3–5.
4. Repeat memory (если exact match → кнопка «Use previous translation»).
5. `Diagnostic` уровня Warning/Error → компактно.
6. `Diagnostic` уровня Info → свёрнуто, только счётчик.

**Если ничего полезного нет — панель схлопывается до 0px** (или тонкий «✓ контекста нет» индикатор на 20px).

**Actions в LineContextPanel — workspace-sensitive** (см. 3.10): одни и те же данные, разные кнопки в режиме перевода vs QC.

### 3.3. Терминология — inline-подсказки без auto-misused

Под edit box, внутри `LineContextPanel`, показываются 3–5 релевантных терминов. **Критически важно:** V1 не пытается автоматически определить «misused» по substring-матчу русского перевода — это даёт ложные срабатывания (см. 3.3.3).

#### 3.3.1. Алгоритм V1

```
1. Нормализация EN source (SanaeNormalizeSource — уже есть)
2. Aho-Corasick по всем term.english_normalized
   → один проход по тексту, находит все вхождения всех терминов
3. Boundary check: для single-word терминов — word boundary;
   для фраз — substring OK
4. Простой ranking (без partial в V1):
     longer exact phrase > shorter exact phrase > exact single word
5. Top 3–5
```

**Ничего больше в V1.** Partial/fuzzy/morphology/history/frequency/recency — только когда реальные данные покажут, что V1 пропускает важные термины. Раннее введение «partial» создаёт шумные совпадения без реальной пользы: Aho-Corasick и так находит только зарегистрированные patterns, так что «partial» в V1 был бы артефактом алгоритма, а не осмысленным классом совпадения.

#### 3.3.2. Разделение heavy/light — ключевая архитектура

EN source не меняется при печати русского перевода. Значит:

- **При `ActiveLineChanged` (смена строки):** один проход Aho-Corasick по EN source → полный список применимых терминов. Кэшируем результат для строки. «Heavy», но один раз.
- **При `wxEVT_STC_MODIFIED` (печать RU):** для каждого закэшированного термина — дешёвый substring check принятого русского варианта в текущем RU тексте → обновить `usage_state`. «Light»: 3–5 substring-поисков по короткой строке.

**Debounce не нужен вообще.** Heavy выполняется один раз на строку, light настолько дёшево, что выполняется синхронно на каждое нажатие без задержки.

```cpp
// При ActiveLineChanged:
cached_term_matches = aho_corasick.Search(normalize(en_source));
// O(en_source.length + matches) — microseconds for one line

// При wxEVT_STC_MODIFIED:
for (auto& match : cached_term_matches) {
    match.usage = check_usage(match.russian, current_ru_text);
}
// O(matches × ru_text.length) — microseconds for 3-5 matches
```

#### 3.3.3. Usage state — без auto-misused в V1

**Проблема substring-матча:** термин `protect → защищать`, переводчик пишет «Я защищу их» — правильный перевод, но literal substring `защищать` отсутствует. Или `King → король`, в строке «с королём». Auto-misused даёт море ложных предупреждений — новая «ненавязчивая» система снова начинает ругаться.

**V1 поведение:**

| Ситуация | Визуальный отклик |
|---|---|
| EN-термин найден в source, точный RU substring есть в переводе | ✓ зелёная галка «правильно использован» |
| EN-термин найден в source, точного RU совпадения нет | **Нейтральная** рекомендация «принятый перевод: защищать [Alt+1]». Никакого warning. |
| EN-термин не найден в source | Термин не показывается |

**Misused как концепция отсутствует в V1.** Она требует aliases/morphology (V2).

#### 3.3.4. Aliases (V1.5, local-only)

Глоссарий расширяется опциональным полем `aliases`:

```
protect → защищать
aliases: защищу, защитил, защитить, защитит, защищай
```

- Aliases проверяются тем же substring-методом, что и основной термин.
- Если matched alias — ✓ (correctly used via alias).
- Если ни основной, ни alias — нейтральная рекомендация.
- Aliases редактируются в `TerminologyEntryPopover` (см. 4.1.4).

V1 работает без aliases (поле опционально, пустое по умолчанию). V2 добавляет morphology поверх.

**Server-sync в V0.3 не включён.** Aliases хранятся только локально в `SanaeLocalProjectConfig[project_id]` (как и QCProfile, см. 3.7/5.10). Добавление поля `aliases` в серверную `TerminologyEntryBody` + `/sync` требует серверной миграции; для V0.3 это отложено. Когда aliases станут server-synced (V0.4+), это будет аддитивное поле — обратно совместимое.

#### 3.3.5. «Не показывать это совпадение снова»

Одна кнопка на чипе термина, без sub-dialog scope. Scope по умолчанию — episode (если термин встречается только в текущем эпизоде) или project (если в нескольких). Переопределяется в Preferences.

### 3.4. Diagnostic vs ReviewIssue — две модели хранения, один UI

**Принцип:** жизненные циклы принципиально разные. CPS сегодня 27 → diagnostic появился; исправил → исчез. Не нужен UUID, комментарии, `resolved_at`, синхронизация. Замечание редактора «здесь сарказм» — наоборот, нужно хранить, обсуждать, синхронизировать.

```cpp
// Новый файл: src/sanae_diagnostic.h
struct SanaeDiagnostic {
    enum class Kind { CpsHigh, CpsLow, Duration, Length, Overlap, Spaces,
                      Tags, Style, Empty, Whitespace, Dash, Quotes,
                      Ellipsis, LineBreaks, Untranslated, Punctuation,
                      TagMalformed, TerminologyDrift, SourceRepeat };
    enum class Severity { Info, Warning, Error };

    Kind kind;
    Severity severity;
    std::string code;
    std::string message;
    AssDialogue *line = nullptr;     // non-owning, transient
    std::string replacement_from;    // for one-click fix, optional
    std::string replacement_to;

    // НЕТ id, НЕТ state, НЕТ comments, НЕТ created_by, НЕТ sync.
    // Вычисляется на лету из текущего состояния строки.
    // Условие исчезло → diagnostic исчезает из списка.
};

// Новый файл: src/sanae_review_issue.h
struct SanaeReviewIssue {
    enum class Kind { Translation, Terminology, Timing, Style, Formatting, Other };
    enum class Severity { Info, Warning, Error };
    enum class State { Open, ReadyForReview, Resolved, WontFix };

    std::string id;                  // UUID, persisted + synced
    Kind kind;
    Severity severity;
    State state;
    std::string body;                // creator's description
    AssDialogue *line = nullptr;     // non-owning, transient (resolved from line_ref)
    std::string line_ref;            // persistent line identity (see 5.7 — SPIKE)
    std::vector<SanaeComment> comments;
    std::string created_by_device_id;
    std::string created_at;
    std::string updated_at;
    std::string resolved_at;
    std::string resolved_by_device_id;
    int version = 0;                  // current entity version (server-side monotonic);
                                      // client sends base_version in PATCH precondition
                                      // (NOT persisted on entity — see server req §2.1)

    // Baseline fingerprints — синхронизируются, см. 3.6
    std::string baseline_text_hash;
    std::string baseline_timing_hash;
};

struct SanaeComment {
    std::string id;
    std::string issue_id;
    std::string body;
    std::string created_by_device_id;
    std::string created_at;
    // Immutable в V0.3 (см. server requirements §7). НЕТ edited_at, НЕТ deleted_at.
};
```

**UI:** `ProblemsList` (в `QCIssueDock` или `LineContextPanel`) показывает оба типа вперемешку, отсортированные по приоритету.

- Diagnostic — серый чип «⚙ вычислено», без автора/состояния. Действие: «Исправить» (one-click fix, если есть `replacement_to`).
- ReviewIssue — цветной чип по `severity`, автор, состояние, иконка 💬 если есть комментарии. Действия зависят от режима (см. 3.10).

### 3.5. State machine ReviewIssue

```
        ┌─────────┐
        │  Open   │ ← создано QC
        └────┬────┘
             │ переводчик нажал «Готово к проверке» (явное действие)
             ▼
   ┌─────────────────┐
   │ ReadyForReview  │ ← переводчик просит перепроверить
   └────────┬────────┘
            │ QC нажал Accept
            ▼
       ┌──────────┐
       │ Resolved │ ← закрыто
       └──────────┘

   Open или ReadyForReview → WontFix (только QC, см. 3.8)
```

**Маппинг клиент↔сервер 1:1:** `open / ready_for_review / resolved / wont_fix`. Никаких клиент-только состояний.

**Transition matrix (полная):**

| Из \ В | open | ready_for_review | resolved | wont_fix |
|---|---|---|---|---|
| **open** | — | ✓ (переводчик: «Готово к проверке») | ✓ (QC: Accept) | ✓ (QC: WontFix, требует `resolution_note`, см. 3.8) |
| **ready_for_review** | ✓ (QC: Return) | — | ✓ (QC: Accept) | ✓ (QC: WontFix, требует `resolution_note`) |
| **resolved** | ✓ (QC: Reopen) | ✗ | — | ✗ |
| **wont_fix** | ✓ (QC: Reopen) | ✗ | ✗ | — |

**Серверные invariantы:**
- `* → wont_fix`: требует `resolution_note` (отдельное поле, НЕ `body`) — непустую строку. Сервер отвергает PATCH с `state=wont_fix` и пустым/отсутствующим `resolution_note` с `422`. См. 3.8.
- `resolved → open` и `wont_fix → open` (Reopen): только QC-устройство (client UX-policy, см. 3.8); сервер не enforce-ит роль. При Reopen сервер **очищает** `resolved_at = null`, `resolved_by_device_id = null`, `resolution_note = null`. Семантика: `resolved_at` означает состояние **текущего** закрытия, не исторического. На следующем Resolve поля выставляются заново. Исторический audit живёт в `ProjectChangeBody` (`entity_type="review_issue"`, `operation="update"`), не в полях сущности. (Authoritative: server req §3.2.)
- Сервер не вычисляет «автоматические» переходы. Все переходы — результат явного PATCH от клиента.

Нет автоматических переходов по сроку давности. Срок давности — вычисляемая UI-метка «давнее», не состояние.

**Семантика полей (важно для контракта):**
- `body` — исходное описание замечания автором («что не так»). Создаётся вместе с issue, может редактироваться.
- `resolution_note` — объяснение, почему решили не исправлять («почему wont_fix»). Обязательно только для `wont_fix`. `null` во всех остальных состояниях. При Reopen из `wont_fix` — сбрасывается в `null`.

### 3.6. modified_after_issue — вычисляемый флаг с sync'd baseline

**Проблема:** QC создал замечание на ПК A → переводчик открыл его на ПК B → ПК B не знает, каким был текст при создании issue → не может вычислить `modified_after_issue`.

**Решение:** baseline fingerprints хранятся **на сервере** как часть `ReviewIssueBody`. Любой клиент может вычислить `current_hash` и сравнить с `baseline_hash`.

#### 3.6.1. Канонический алгоритм хеширования

Чтобы два разных клиента не получили разные хеши одной и той же строки, алгоритм зафиксирован строго:

**`baseline_text_hash`** — хеш видимого текста строки:

```
1. Взять line.Text (полный ASS-текст строки, включая override-теги)
2. Удалить все override-блоки {\...} — оставить только видимый текст
3. Сохранить \N как есть (literal backslash-N, два байта 0x5C 0x4E)
4. Не применять Unicode-нормализацию (NFKC и т.п.) — сравнение exact
5. Не тримить whitespace (trailing/leading пробелы сохраняются)
6. Закодировать как UTF-8
7. sha256 от полученных байт, hex-строка lowercase, 64 символа
```

Псевдокод:
```cpp
std::string compute_text_hash(AssDialogue const* line) {
    std::string visible = strip_override_blocks(line->Text);  // удаляет {...}
    // visible сохраняет \N, сохраняет whitespace, не нормализуется
    std::string utf8 = visible;  // уже UTF-8 (Aegisub хранит как UTF-8)
    return sha256_hex(utf8);
}
```

**`baseline_timing_hash`** — хеш таймингов строки:

```
1. Взять line.Start и line.End
2. Канонизировать в centiseconds (integer, 1 cs = 10 ms):
   canonical_start_cs = to_centiseconds(line.Start)
   canonical_end_cs   = to_centiseconds(line.End)
   где to_centiseconds приводит ЛЮБОЕ внутреннее представление Aegisub
   (int centiseconds, agi::Time, миллисекунды, и т.д.) к целому числу centiseconds.
   Это сознательная граница хеширования: даже если внутренний тип изменится
   или второй клиент использует другое представление, hash остаётся стабильным.
3. Каноническая строка: "<start_decimal>|<end_decimal>"
   где start_decimal и end_decimal — десятичные строки без ведущих нулей,
   без знака (значения всегда >= 0), без суффикса
   пример: "1020|1140" для Start=1020cs, End=1140cs
4. Закодировать как ASCII
5. sha256, hex lowercase
```

Псевдокод:
```cpp
std::string compute_timing_hash(AssDialogue const* line) {
    int start_cs = to_centiseconds(line->Start);  // canonical conversion
    int end_cs   = to_centiseconds(line->End);
    std::string s = std::to_string(start_cs) + "|" + std::to_string(end_cs);
    return sha256_hex(s);  // ASCII input
}
```

**Замечание о `to_centiseconds`:** точное внутреннее представление `line->Start`/`line->End` в Aegisub (int centiseconds, `agi::Time`, или другое) **должно быть подтверждено в Phase 0.12** прямым чтением `src/ass_dialogue.h` / `src/ass_time.h`. Независимо от результата, hash boundary использует канонические centiseconds через `to_centiseconds()`, а не сырой внутренний тип. Это делает контракт устойчивым к изменениям внутреннего представления.

**Канонические правила для всех клиентов (Sanae и любые будущие):**
- UTF-8 для текста, ASCII для таймингов
- Centiseconds (не миллисекунды, не секунды, не SMPTE timecode)
- Без Unicode-нормализации (сравнение exact; нормализация была бы для fuzzy-матчинга, не для identity)
- `\N` сохраняется как два байта, не конвертируется в newline
- Override-блоки `{\...}` удаляются полностью, включая содержимое
- Whitespace не тримится
- Hex lowercase

Эти правила фиксируются в отдельном `Sanae Baseline Fingerprint Spec` (короткий документ, ~1 страница) и цитируются в OpenAPI-описании `baseline_text_hash`/`baseline_timing_hash`.

#### 3.6.2. Использование fingerprints

```cpp
// При создании замечания (любой клиент):
issue.baseline_text_hash = compute_text_hash(line);    // см. 3.6.1
issue.baseline_timing_hash = compute_timing_hash(line);
// Сохраняется на сервере.

// На любом клиенте, на AssFile::COMMIT_DIAG_* (changed == issue.line):
if (issue.kind == Translation || issue.kind == Terminology || ...) {
    if (compute_text_hash(line) != issue.baseline_text_hash)
        issue.modified_after_issue = true;
}
if (issue.kind == Timing) {
    if (compute_timing_hash(line) != issue.baseline_timing_hash)
        issue.modified_after_issue = true;
}
```

`modified_after_issue` — **клиентский вычисляемый флаг**, не хранится на сервере. Baseline fingerprints — хранятся, синхронизируются.

**Ограничение:** fingerprints детектируют факт изменения, но не его семантику. «Переводчик изменил запятую» и «переводчик полностью переписал строку» оба дают `modified_after_issue=true`. Это сознательно — семантику «исправлено ли замечание» оценивает QC, не алгоритм.

### 3.7. QC-профили с пресетами

`RU == EN` легитимен для NASA/OK/никнеймов. `!!` может быть стилем. Правила не универсальны — но обычного переводчика нельзя заставлять настраивать 15 параметров (это воссоздаст UI, от которого уходим).

**Пресеты:**

```cpp
// Новый файл: src/sanae_qc_profile.h
enum class QCProfilePreset { TeamStandard, StrictQC, MinimalQC, Custom };

struct SanaeQCProfile {
    QCProfilePreset preset = QCProfilePreset::TeamStandard;

    // Типографика
    enum class QuoteStyle { Off, Guillemets, Straight, Curly };
    QuoteStyle quotes = QuoteStyle::Guillemets;

    enum class DashStyle { Off, EmDash, Hyphen };
    DashStyle dashes = DashStyle::EmDash;

    enum class EllipsisStyle { Off, Char, ThreeDots };
    EllipsisStyle ellipsis = EllipsisStyle::Char;

    // CPS / длина
    int cps_error_threshold = 25;
    int cps_warning_threshold = 20;
    int cps_low_threshold = 5;       // info, не блокирует
    int max_line_length = 42;
    int max_line_breaks = 2;

    // Спец-проверки
    enum class SeverityLevel { Off, Info, Warning, Error };
    SeverityLevel untranslated_check = SeverityLevel::Warning;
    SeverityLevel repeated_punctuation = SeverityLevel::Info;
    SeverityLevel empty_line = SeverityLevel::Error;
    SeverityLevel whitespace = SeverityLevel::Warning;
    // ...
};
```

**UI:** dropdown с 4 пресетами. Параметры спрятаны под «Расширенные настройки QC» (раскрываются только при `Custom`). 95% переводчиков выбирают «Стандарт команды» и никогда не видят параметров.

- Хранится в `SanaeLocalProjectConfig[project_id]` (project-scoped local cache, НЕ per-file sidecar — см. 5.10). **Server-sync в V0.3 НЕ включён** — QCProfile остаётся local-only до отдельного решения. Причина: конфигурация команды редко меняется, а добавление поля в `ProjectBody` + `/sync` требует серверной миграции, которую можно отложить. Multi-device команды настраивают профиль на каждой машине один раз (конфигурация сохраняется в project-scoped cache, не дублируется по эпизодам).
- Дефолт — «Стандарт команды».
- **Info-правила никогда не блокируют готовность.** Warning — показываются, не блокируют. Error — блокируют «Submit for QC» (настраиваемо: можно отключить блокировку для конкретного правила).

### 3.8. WontFix — UX-policy + client workflow role (не server-enforced authorization)

Переводчик не должен иметь возможности просто закрыть замечание QC как «Не исправлять». QC написал «смысл переведён неправильно» — переводчик не может это проигнорировать.

#### 3.8.1. Client workflow role

`WorkspaceMode` не может служить ролью: пользователь может просто переключить интерфейс в QC. Вводится отдельная client-side workflow role:

```cpp
// Новый файл: src/sanae_user_role.h
enum class SanaeUserRole {
    Translator,
    Reviewer
};
```

Это **не security/RBAC** — это client-side workflow role, определяющая, какие UI-действия доступны. Устанавливается:
- через Preferences (пользователь выбирает свою роль один раз),
- или через переключатель в тулбаре (быстрая смена для тех, кто работает и как переводчик, и как QC).

`SanaeUserRole` комбинируется с `WorkspaceMode`: например, `Reviewer` + `QC mode` → полный набор QC-действий; `Translator` + `QC mode` → просмотр без Accept/WontFix.

#### 3.8.2. Поведение по ролям (UX-policy)

| Роль | Действие с ReviewIssue |
|---|---|
| `Translator` | «Не согласен» + комментарий (state НЕ меняется, добавляется comment) |
| `Reviewer` | «Не исправлять» → `WontFix` (с обязательным `resolution_note`, см. transition matrix 3.5) |

**Важно: это UX-policy, а не server-enforced authorization.** Сервер v0.3 не вводит RBAC и не знает ролей. Любое устройство технически может отправить `PATCH /review-issues/{id}` с `state=wont_fix`. Для доверенной маленькой команды это приемлемо: ограничение живёт в клиентском UI, и злонамеренный клиент может его обойти, но нормальные клиенты — нет.

#### 3.8.3. `resolution_note` (отдельное поле, НЕ `body`)

Серверный `ReviewIssueBody` содержит отдельное поле:

```yaml
resolution_note:
  type: string
  nullable: true
  description: >
    Explanation of why the issue was decided not to be fixed.
    REQUIRED when state=wont_fix (server rejects with 422 if missing/empty).
    MUST be null in all other states.
    Reset to null on Reopen from wont_fix.
```

Семантика:
- `body` = что не так (исходное описание автора)
- `resolution_note` = почему решили не исправлять

Это разделение критично: иначе замечание с уже непустым `body` формально пройдёт `wont_fix` без объяснения причины.

#### 3.8.4. `AllowTranslatorWontFix`

`AllowTranslatorWontFix` **НЕ хранится в локальных Preferences** (иначе переводчик сам сможет включить). Hardcoded `false` в коде клиента до V0.4. Когда V0.4 добавит server-sync QCProfile (см. 5.10), эта настройка станет частью серверной конфигурации проекта.

**Если команде нужна настоящая authorization** (переводчик не может обойти ограничение даже модифицированным клиентом) — это требует серверной модели ролей/permissions, что явно outside scope v0.3. В этом случае следует открыть отдельный spike после v0.3.

### 3.9. Workflow эпизода + серверный review_state

**Проблема multi-device:** переводчик работает на ПК A, QC на ПК B. Если «Submit for QC» — только sidecar, ПК B никак не узнает, что серия перешла в In Review. `ReviewIssue` это не решает: серия может быть отправлена на проверку ещё до появления первого замечания.

**Решение:** серверное поле `episode.review_state` + endpoint для переходов.

```
Translation (review_state=translating)
  ↓ [Submit for QC — серверное событие]
In Review (review_state=in_review) ← ПК B видит
  ↓ [QC создаёт ReviewIssues — клиентски выводит needs_fixes]
Needs Fixes (display, auto from open ReviewIssues)
  ↓ [переводчик исправляет, переводит issues в ReadyForReview — клиентски выводит re_review]
Re-review (display, auto from ReadyForReview ReviewIssues)
  ↓ [QC принимает все ReviewIssues → Resolved]
Done (review_state=done — серверное событие)
  ↓ [явный Finalize — отдельное действие, может быть позже]
Finalized (EpisodeBody.status=finalized — уже в v0.2)
```

**Сервер хранит явно:** `translating` / `in_review` / `done`.
**Клиент выводит display state:** если `review_state==in_review` и есть open ReviewIssue → показываем «Needs Fixes»; если есть ReadyForReview → «Re-review».

Это избегает серверной логики вывода состояний, но даёт multi-device видимость.

#### 3.9.1. Transition matrix для episode.review_state

Чтобы убрать неоднозначности (случайный Submit, возврат из in_review, повторное открытие done), переходы зафиксированы явно:

| Из \ В | translating | in_review | done |
|---|---|---|---|
| **translating** | — | ✓ (Submit for QC) | ✗ (требует прохода через in_review) |
| **in_review** | ✓ (Return to translator — отмена отправки) | — | ✓ (Mark Done — только если нет open/ready_for_review ReviewIssues, см. инвариант ниже) |
| **done** | ✗ | ✓ (Reopen for QC — повторное открытие) | — |

**Серверные invariantы:**
- `in_review → done`: сервер отвергает с `409` (`error.code="open_issues_exist"`) если у эпизода есть хотя бы один **non-deleted** ReviewIssue в `state=open` или `state=ready_for_review`. Проверка буквально: `state IN ('open','ready_for_review') AND deleted_at IS NULL`. Soft-deleted issues (с `deleted_at`) НЕ блокируют done, независимо от их state.
- `translating → done`: запрещён напрямую (нет смысла помечать сделанным без проверки). Сервер отвергает с `422`.
- `done → translating`: запрещён (если эпизод уже готов, возврат к работе переводчика бессмысленен — нужно `done → in_review`).
- `done → in_review`: разрешён без ограничений (QC может повторно открыть для дополнительной проверки).
- `in_review → translating`: разрешён без ограничений (отмена случайной отправки).
- **`done → POST /review-issues`**: запрещён. Сервер отвергает создание нового ReviewIssue с `409` (`error.code="episode_review_done"`), если `episode.review_state == done`. Клиент сначала должен перевести `done → in_review`, затем создавать issue. Это сохраняет сильный invariant: `done` действительно означает отсутствие незакрытых ReviewIssues. (См. server requirements §4.2, §6.1.)
- Сервер не вычисляет display-состояния (`needs_fixes`, `re_review`) — они клиентские.

**Invariant с Finalize (явный выбор):**
- **Finalize НЕ требует `review_state=done`.** Это сознательное решение: иногда нужно финализировать техническую ревизию (например, обновить compact RUSUB после исправления таймингов) без полного прохождения QC-цикла.
- Однако клиент по умолчанию **показывает warning** при попытке Finalize, если `review_state != done`: «Эпизод не помечен как готовый. Финализировать всё равно?» Пользователь может продолжить.
- Это warning, не блокировка. Команда может включить строгий режим через future server-side QCProfile setting `RequireDoneBeforeFinalize` (V0.3 не включает; см. 5.10).

**Submit for QC ≠ Finalize.** Finalize — серверная мутация (compact RUSUB + term ops + new FinalizedRevision). «Submit for QC» — workflow-событие между людьми. `Ctrl+Enter` = Submit (локальный триггер серверного перехода `translating → in_review`), **не** Finalize. Finalize — отдельная команда с явным подтверждением и optional warning (см. выше).

### 3.10. LineContextPanel — workspace-sensitive actions

Одни и те же данные в `LineContextPanel`, разные action-кнопки по режиму:

| Элемент | В режиме Перевод | В режиме QC |
|---|---|---|
| ReviewIssue (open) | [Готово к проверке] [Ответить] | [Принять] [Вернуть] [Комментарий] |
| ReviewIssue (ready_for_review) | [Ответить] | [Принять] [Вернуть] |
| Термин | [Alt+1] применить | [Alt+1] применить (тоже) |
| Diagnostic | [Исправить] (one-click fix) | [Исправить] (тоже) |
| Repeat (exact) | [Use previous translation] | [Use previous translation] |

Это сочетается с hotkey-контекстом `Sanae QC` (см. 4.8): `Q/C/Enter/Backspace` работают только в QC-режиме и не работают при фокусе в edit box.

---

## 4. Подробные изменения по направлениям

### 4.1. Терминология — полный ревамп

#### 4.1.1. Inline-подсказки в LineContextPanel

**Как сейчас:** терминология доступна только через модальные диалоги. Никакого поиска при вводе.

**Как должно быть:** внутри `LineContextPanel` (раздел 3.2), приоритет 3 (после ReviewIssue, перед repeats). Обновляется по `ActiveLineChanged` (heavy, Aho-Corasick) + `wxEVT_STC_MODIFIED` (light, usage check). Без debounce.

**Файлы:**
- **Новый:** `src/terminology_hint_panel.h/.cpp` — подсекция `LineContextPanel`.
- **Новый:** `src/sanae_terminology_index.h/.cpp` — Aho-Corasick + кэш.
- **Изменить:** `src/subs_edit_box.cpp:200-218` — добавить `LineContextPanel` (включает term hints).
- **Изменить:** `src/sanae_project.h` — `MatchTerminologyForLine(AssDialogue*)` метод.

#### 4.1.2. Aho-Corasick индекс

**Как сейчас:** flat vector + `std::find_if`, O(T) на каждый lookup.

**Как должно быть:** Aho-Corasick trie, строится один раз при `LoadSnapshot`/`SyncProject`, инвалидация по версии `terminology + terminology_drafts`.

```cpp
class SanaeTerminologyIndex {
public:
    void Rebuild(std::vector<SanaeTerminologyEntry> const& terms,
                 std::vector<SanaeTerminologyDraft> const& drafts);
    std::vector<SanaeTerminologyMatch> Search(std::string const& normalized_en_text) const;
private:
    AhoCorasickTrie trie_;  // built from term.english_normalized
    // ...
};
```

**Файлы:**
- **Новый:** `src/sanae_terminology_index.h/.cpp` — trie + Search.
- **Изменить:** `src/sanae_project.cpp:2661-2863, 2865-2945, 505-507, 3023-3112` — использовать индекс + `english_normalized`.

#### 4.1.3. Hunspell singleton + background GenerateCandidates

**Как сейчас:** `SpellCheckerFactory::GetSpellChecker()` на каждый `GenerateCandidates` — disk I/O + memory mapping.

**Как должно быть:** singleton в `SanaeProjectManager`, ленивая инициализация, подписка на `Tool/Spell Checker/Language`. `GenerateCandidates` — в `agi::dispatch::Background` с прогрессом, кэш по версии.

**Файлы:**
- **Изменить:** `src/sanae_project.cpp:2745, 2661-2863` — singleton + background + кэш.

#### 4.1.4. TerminologyEntryPopover (объединить TermEditDialog + TerminologyEntryDialog)

**Как сейчас:** два модальных окна для одной задачи.

**Как должно быть:** один `TerminologyEntryPopover` (`wxPopupTransientWindow`), 3 поля (EN/RU/Note) + опциональные aliases (V1.5). `Ctrl+T` из edit box открывает с предзаполнением.

**Файлы:**
- Удалить `dialog_sanae_terminology.cpp:62` `TermEditDialog` и `dialog_sanae_final_review.cpp:39` `TerminologyEntryDialog`.
- **Новый:** `src/terminology_entry_popover.h/.cpp`.
- **Изменить:** `command/sanae.cpp:415` — вызывать поповер.

#### 4.1.5. Удаление TerminologyCandidateDialog

`dialog_sanae_terminology.cpp:106` — функционально дубликат страницы Candidates в `FinalReviewDialog`. Удалить.

### 4.2. QC — Diagnostic + ReviewIssue

#### 4.2.1. Единый ProblemsList в QCIssueDock

**Как сейчас:** `FinalReviewDialog` — модальное окно 1080×720, 6 страниц, блокирует сетку.

**Как должно быть:** `QCIssueDock : public wxPanel` — встраивается в `ToolsSizer` или отдельный сплиттер. Переключается в режим QC (`Ctrl+Shift+2`).

**Структура:**
```
┌─ QC — Эп. 05 ────────────────────────┐
│ 7 проблем · 4 требует · 2 ждут · 1⚠  │
│ [☐ критич.] [☐ открытые] [⚙]         │
├───────────────────────────────────────┤
│ ⚙ CPS 27 (high)       | L13 | 00:45   │ ← Diagnostic, серый
│ 💬 Сарказм (ready_for_review) | L13   │ ← ReviewIssue, автор
│ ⚙ Overlap with L12    | L14           │
│ ✓ Принять  ↩ Вернуть  ✎ Комментарий  │ ← workspace-sensitive
├───────────────────────────────────────┤
│ [F4] Следующая требующая действия    │
└───────────────────────────────────────┘
```

- Клик → `SetSelectionAndActive({line}, line)` + `videoController->JumpToTime(line->Start)`. **Не модальный** — пользователь сразу редактирует.
- `F4` — «Следующая требующая действия проблема», контекстная навигация (см. ниже).
- `Shift+F4` — предыдущее. `Ctrl+F4` — следующее критическое.
- Список виртуализирован (`wxLC_VIRTUAL` или `wxDataViewCtrl`).

**Контекстная навигация `F4`:**

| Роль / Режим | `F4` переходит к |
|---|---|
| Translator (Translation mode) | следующему `Open` ReviewIssue **или** blocking Error Diagnostic |
| Reviewer (QC mode) | следующему `ReadyForReview` ReviewIssue; если таких нет → следующему `Open` |

Обоснование: для QC самая важная очередь — `ReadyForReview` (переводчик исправил и просит перепроверить). Для переводчика — `Open` (что ему назначено) и blocking errors (что мешает работе).

**Фильтры:** `[☐ критич.]` (severity=Error), `[☐ открытые]` (state=open ИЛИ ready_for_review). Фильтр «мои» **убран** — сервер v0.3 не имеет user identity, только device_id; один QC с ноутбука и десктопа = два device_id, фильтр «мои» был бы ложным. Расширенные фильтры — в popover `[⚙]`.

**Файлы:**
- **Новый:** `src/qc_issue_dock.h/.cpp`.
- **Удалить (или опциональный fallback):** `src/dialog_sanae_final_review.cpp`.
- **Изменить:** `src/frame_main.cpp:187-219` — добавить `qc_dock`.
- **Изменить:** `src/command/sanae.cpp:382-388` — переключать в QC-режим.

#### 4.2.2. Расширенные auto-QC проверки (Diagnostic)

**Как сейчас:** 7 проверок в `TranslationProject::CheckLine` (`translation_project.cpp:904-949`).

**Как должно быть:** расширить, но с уровнями severity и **настраиваемостью через QCProfile** (раздел 3.7). Новые проверки:

| Проверка | Severity (default) | Код | Конфигурируемо |
|---|---|---|---|
| Пустая строка | Error | `empty` | QCProfile.empty_line |
| Trailing/leading whitespace | Warning | `whitespace` | QCProfile.whitespace |
| Длинное тире `—` vs дефис `-` | Info | `dash` | QCProfile.dashes |
| Типографские кавычки | Info | `quotes` | QCProfile.quotes |
| `...` vs `…` | Info | `ellipsis` | QCProfile.ellipsis |
| CPS слишком низкий (< 5) | Info | `cps_low` | QCProfile.cps_low_threshold |
| Слишком много `\N` (> 2) | Warning | `line_breaks` | QCProfile.max_line_breaks |
| RU == EN (untranslated) | Warning | `untranslated` | QCProfile.untranslated_check |
| Повтор пунктуации (`!!`, `??`) | Info | `punctuation` | QCProfile.repeated_punctuation |
| Подозрительный ASS-тег | Warning | `tag_malformed` | — |

**Info-правила никогда не блокируют готовность.** Warning — показываются, не блокируют. Error — блокируют «Submit for QC» (настраиваемо).

**Файлы:**
- `src/translation_project.cpp:904-949` — расширить `CheckLine`.
- **Новый:** `src/sanae_qc_checks.h/.cpp` — вынести проверки для тестируемости.
- **Новый:** `src/sanae_qc_profile.h/.cpp` — профиль + пресеты.

#### 4.2.3. Быстрое создание ReviewIssue

**Как сейчас:** не существует — reviewer не может оставить комментарий на строку.

**Как должно быть:** в режиме QC, выделить строку, нажать `Q`:

```
┌─ Замечание на L13 ──────────────────┐
│ Тип: [перевод ▾]  Серьёзность: ⚠   │
│ Что не так?                         │
│ [________________________________]  │
│ [Enter] Отправить  [Esc] Отмена     │
└─────────────────────────────────────┘
```

Тип предлагается по контексту (timing issue → `timing`, terminology issue → `terminology`, иначе `translation`). После `Enter` — `SanaeReviewIssue{state=Open, baseline_*=hash(line)}` сохраняется локально + отправляется на сервер.

**Файлы:**
- **Новый:** `src/qc_quick_issue_popover.h/.cpp`.
- **Новый:** команда `sanae/qc/create_issue` с биндингом `Q` в контексте `Sanae QC`.

### 4.3. Статусы и workflow

#### 4.3.1. Упрощение ReviewStatus

**Как сейчас:** 9-значный enum `ReviewStatus` (`translation_project.h:23`), ручной.

**Как должно быть:** большинство переходов автоматические, но «Готово к проверке» — явное действие переводчика.

| Состояние | Когда устанавливается |
|---|---|
| `Untranslated` | Изначально (без текста) |
| `Draft` | Пользователь ввёл текст |
| `NeedsContext` | Пользователь отметил `?` (одно нажатие) |
| `MeaningChecked` | Все `Error`-severity Diagnostics на строке устранены + пользователь нажал «Готово к проверке» на строке |
| `QCPassed` | Нет `ReviewIssue` на строке в состоянии `open` **или** `ready_for_review` (остались только `resolved`/`wont_fix`) **И** нет blocking `Error`-severity Diagnostics |
| `Final` | Эпизод `Finalize`-нут (серверный `EpisodeBody.status == "finalized"`) |

`OCRDoubt`, `Polished`, `Typeset` — остаются ручными в расширенном режиме.

#### 4.3.2. Episode review workflow

**Как сейчас:** только `EpisodeBody.status` (`translating/finalized/archived`) на сервере.

**Как должно быть:** серверное `review_state` поле (раздел 3.9) + явные команды «Submit for QC» (`Ctrl+Enter`) и «Mark Done» (QC). Finalize — отдельная команда.

**Файлы:**
- `src/translation_project.cpp:562-603` — авто-переходы.
- **Новый:** `src/sanae_status_machine.h/.cpp`.
- **Новый:** команда `sanae/episode/submit_for_qc` (вызывает серверный review-transition).
- **Изменить:** `src/dialog_translation.cpp:549-550` — заменить ручной статус-выбор на авто-индикатор.

### 4.4. Главный экран и LineContextPanel

#### 4.4.1. Режимы

**Как сейчас:** фиксированные `wxBoxSizer`, ничего не докается.

**Как должно быть:**
- `WorkspaceMode` enum + `SetWorkspaceMode`.
- `wxSplitterWindow` между `videoBox` и `ToolsSizer` (гибкая граница).
- `LineContextPanel` под edit box (раздел 3.2).
- `QCIssueDock` слева от сетки в режиме QC.
- Focus mode (`Ctrl+Shift+F`): скрыть тулбар/аудио/меню, оставить видео + edit box + LineContextPanel.
- EN-строка над RU-редактором по умолчанию.

**Файлы:**
- `src/frame_main.cpp:78-80, 187-219` — режимы, сплиттер, док-панели.
- `src/frame_main.h` — `WorkspaceMode current_mode`, `QCIssueDock *qc_dock`, `LineContextPanel *context_panel`.
- **Новый:** `src/workspace_mode.h`.
- **Новый:** `src/line_context_panel.h/.cpp` — приоритетная панель (раздел 3.2).
- `src/subs_edit_box.cpp:200-218` — EN-строка + context_panel.
- `libresrc/default_hotkey.json` — `Ctrl+Shift+1/2/3`, `Ctrl+Shift+F`.

#### 4.4.2. Преобразование модальных диалогов в док-панели

| Диалог | Замена |
|---|---|
| `FinalReviewDialog` | `QCIssueDock` |
| `TerminologyDialog` | `TerminologyDock` (панель, открывается в расширенном режиме) |
| `ProjectDialog` | `ProjectNavigatorDock` |
| `ProjectSearchDialog` | `ProjectSearchDock` |
| `RepeatHistoryDialog` | Инлайн в `LineContextPanel` |
| `SemanticDiffDialog` | `DiffDock` |
| `CreateSeason/ProjectDialog` | Inline form в `ProjectNavigatorDock` |
| `TerminologyCandidateDialog` | **Удалить** (дубликат) |
| `EpisodeDetailsDialog` | Свойства в `ProjectNavigatorDock` + recovery modal |
| `ConnectionDialog`, `SanaeBatchImportDialog`, `SanaeUpdateDialog`, `VersionCheckerResultDialog` | Оставить |

**Правило оставшихся модалей:** только (a) одноразовая критическая настройка, (b) длинный wizard, (c) нужно остановить пользователя для важного решения.

### 4.5. Производительность

Все оптимизации — **после Phase 0 instrumentation** на реальном тяжёлом проекте.

**(a) Инкрементальный refresh при смене активной строки.** `BaseGrid::OnActiveLineChanged` (`base_grid.cpp:278-293`) — заменить `Refresh(false)` на `RefreshRect` для предыдущей и новой активной строки.

**(b) Кэш ширины колонок.** `SetColumnWidths` (`base_grid.cpp:755-777`) — инвалидировать только для изменённых стилей/actors/effects. Для `COMMIT_DIAG_TEXT` не вызывать.

**(c) Lazy `RebuildUnits`.** На `COMMIT_DIAG_ADDREM` одной строки — инкрементальное обновление `units` map. Полная перестройка — только на `COMMIT_NEW`/`COMMIT_ORDER`.

**(d) Background `GenerateCandidates`/`TerminologyConsistencyIssues`.** `agi::dispatch::Background`, кэш по версии `terminology + terminology_drafts + memory + current_ass_revision`.

**(e) Async `Finalize`.** `agi::dispatch::Background` + `wxProgressDialog` (modeless). `pending_finalize_key` не сбрасывать на `COMMIT_DIAG_TEXT` без изменения normalized text.

**(f) Профилирование.** RAII-таймеры в ключевых точках (раздел 8.2).

**Файлы:**
- `src/base_grid.cpp:278-293, 186-211, 755-777`.
- `src/grid_column.cpp:244-246`.
- `src/translation_project.cpp:250-265`.
- `src/sanae_project.cpp:3186-3288, 1885-1893`.
- **Новый:** `src/sanae_profiling.h`.

### 4.6. Минимизация модальных окон

Аудит в разделе 2.9. План — в разделе 4.4.2.

### 4.7. Локализация

**(a) Fuzzy-фиксы.** `po/ru.po:6912` (`Occurrences` → «Вхождения»), `po/ru.po:6985` (`Find new terms…` → «Найти новые термины…»). Аудит остальных fuzzy.

**(b) Убрать хрупкий `wxString::Replace`.** `sanae_project.cpp:2934-2939` и `dialog_sanae_final_review.cpp:80-88` — форматные строки с позиционными аргументами:

```cpp
// было:
detail << "Accepted: " << term.russian << ". In this line: "
    << (use.actual.empty() ? "not detected" : use.actual) << ".";

// стало:
auto actual_text = use.actual.empty() ? _("not detected").utf8_str() : use.actual;
detail = agi::wxformat(_("Accepted: %s. In this line: %s."),
    term.russian, actual_text).utf8_str();
```

**(c) Стандартизировать термины UI:**

| EN | RU |
|---|---|
| Finalize | Финализировать |
| Recovery Snapshot | Резервная копия |
| Terminology History | История терминов |
| Ignored Candidate | Скрытый кандидат |
| Revision | Редакция |
| Sync Project | Синхронизировать проект |
| Source File | Исходный файл |
| Episode | Серия |
| Season | Сезон |
| Issue | Замечание |
| Review | Ревью / Проверка |
| Candidate | Кандидат |
| Diagnostic | Диагностика |
| Submit for QC | Передать на проверку |

**(d) Внутренние имена классов/API не меняем.** Только UI-строки.

**Файлы:**
- `po/ru.po` — fuzzy-фиксы, глоссарий, новые строки.
- `src/dialog_sanae_final_review.cpp:80-88`, `src/sanae_project.cpp:2934-2939` — форматные строки.

### 4.8. Горячие клавиши

**Шаг 0 — аудит существующих hotkey (обязательный).** Прочитать `libresrc/default_hotkey.json`, составить карту занятых комбинаций по контекстам (`Always / Audio / Default / Styling Assistant / Subtitle Edit Box / Subtitle Grid / Translation Assistant / Video`). Только после этого назначать новые.

**Назначения:**

| Клавиша | Контекст | Действие |
|---|---|---|
| `Alt+1`…`Alt+5` | Subtitle Edit Box | Применить термин 1..5 (или user-configurable) |
| `Ctrl+T` | Subtitle Edit Box | Добавить термин (popover) |
| `Ctrl+I` | Subtitle Edit Box | Игнорировать совпадение |
| `Ctrl+Shift+1` | Always | Режим перевода |
| `Ctrl+Shift+2` | Always | Режим QC |
| `Ctrl+Shift+3` | Always | Расширенный режим |
| `Ctrl+Shift+F` | Always | Focus mode |
| `F4` | Sanae QC (новый контекст) | Следующая проблема |
| `Shift+F4` | Sanae QC | Предыдущая проблема |
| `Ctrl+F4` | Sanae QC | Следующее критическое |
| `Q` | Sanae QC | Создать замечание на строке |
| `Enter` | Sanae QC | Принять замечание |
| `Backspace` | Sanae QC | Вернуть замечание |
| `C` | Sanae QC | Комментарий к замечанию |
| `Ctrl+Enter` | Translation | Передать на QC (НЕ Finalize) |

**Критическое правило:** QC-команды (`Q`, `C`, `Enter`, `Backspace`) активны **только** в новом контексте `Sanae QC`, который включается при `WorkspaceMode == QC` **И** фокус не в `SubsEditBox`. `1…5` без модификаторов — никогда не команда в текстовом поле.

**Файлы:**
- `libresrc/default_hotkey.json` — добавить все.
- **Новый:** `src/command/qc.cpp` — QC-команды.
- **Новый:** `src/command/terminology_inline.cpp`.

### 4.9. Спокойный интерфейс

Визуальная иерархия:

| Ситуация | Визуальный отклик |
|---|---|
| Обычная строка, без проблем | Базовый цвет, без индикаторов |
| Строка с `Error` Diagnostic | Тонкая красная полоска слева (4 px), без заливки |
| Строка с `Warning` Diagnostic | Тонкая жёлтая полоска слева |
| Строка с `Info` Diagnostic | Без полоски, только иконка в колонке QC (если показана) |
| Строка с `Open` ReviewIssue | Тонкая синяя полоска + иконка 💬 |
| Строка с `ReadyForReview` ReviewIssue | Тонкая зелёная полоска (для QC: «ждёт проверки») |
| Строка с `modified_after_issue` | Маленький индикатор «✎ изменено» рядом с ReviewIssue |
| Строка `Resolved` | Без индикаторов |
| Повтор-источник (exact) | Существующий blend 20% (`base_grid.cpp:469-479`) |
| Повтор-источник (similar) | Существующий blend 14% |

**Не показывать** постоянные badge-счётчики, если пользователь ничего не должен сделать прямо сейчас. Счётчик проблем — в `QCIssueDock` шапке, не в тулбаре. Тосты — только для успешных действий, требующих подтверждения.

**Файлы:**
- `src/base_grid.cpp:443-514` — рисование полосок.
- `src/grid_column.cpp:263-282` — уровни QC.
- **Новый:** `src/sanae_visual_hierarchy.h`.

---

## 5. Серверные дополнения v0.3 — обзор (authoritative: server requirements)

> **ВНИМАНИЕ:** Этот раздел — краткий обзор для клиента. **Authoritative спецификация серверного wire/API — `SANAE_SERVER_REQUIREMENTS_v0.3.md`.** При любом расхождении приоритет у server requirements. Этот раздел НЕ дублирует schemas/endpoints; он описывает только то, что клиенту нужно знать для архитектуры.

### 5.1. Почему это нужно

Сервер v0.2 хранит файлы как непрозрачные блобы и глоссарий. Без per-line сущности и без episode review workflow multi-device QC невозможен: два устройства не видят замечания друг друга; ПК B не знает, что серия переведена в «In Review» на ПК A.

Без серверной `ReviewIssue` + `Comment` + `episode.review_state`:
- QC не может оставить замечание, к которому переводчик вернётся после перезапуска.
- Нельзя увидеть «кто и когда» оставил замечание.
- Нельзя узнать, что эпизод на проверке, без копирования сайдкара.

### 5.2. Новые серверные сущности (кратко)

| Сущность | Назначение | Подробности |
|---|---|---|
| `ReviewIssueBody` | Persistent человеческое замечание с state machine, baseline fingerprints, soft-delete | server req §2.1, §3 |
| `LineCommentBody` | Immutable append-only комментарий к ReviewIssue (нет edit/delete в v0.3) | server req §2.2, §7 |
| `EpisodeBody.review_state` | Workflow state эпизода: `translating / in_review / done` + `review_state_version` | server req §2.3, §4 |
| `ProjectSyncResponse` расширения | `review_issues`, `review_comments`, `server_version`, `server_capabilities` массивы | server req §2.4, §18 |
| `ProjectChangeBody` новые `entity_type` | `"review_issue"`, `"review_comment"`, `"episode_review_state"` | server req §2.5 |

### 5.3. Ключевые server-enforced invariantы (кратко)

Клиент должен корректно обрабатывать эти серверные проверки (полная спецификация — server req §3, §4, §6):

- **ReviewIssue state machine:** `open → ready_for_review → resolved` + `wont_fix`. `* → wont_fix` требует non-empty `resolution_note` (отдельное поле, НЕ `body`). Reopen из `resolved`/`wont_fix` очищает `resolved_at`/`resolved_by_device_id`/`resolution_note` (server req §3.2).
- **`modified_after_issue`:** клиентский вычисляемый флаг из `baseline_text_hash`/`baseline_timing_hash` (server req §9). Сервер хранит fingerprints, не вычисляет флаг.
- **Episode `in_review → done`:** сервер отвергает `409 open_issues_exist` если есть non-deleted ReviewIssue в `open`/`ready_for_review` (server req §4.2).
- **`done → POST /review-issues`:** запрещён, `409 episode_review_done` (server req §4.2, §6.1). Сильный invariant: `done` = нет незакрытых issues.
- **Optimistic concurrency:** `base_version` (ReviewIssue PATCH/DELETE) и `base_review_state_version` (episode transition) — client-supplied preconditions. `version`/`review_state_version` — persisted server state (server req §2.1, §2.3).
- **Idempotency:** `Idempotency-Key` + operation fingerprint = `SHA256(method + path + canonical body)` (server req §15).
- **Capability detection:** v0.3 сервер возвращает `server_capabilities` массив в `/sync`; клиент включает `ServerReviewSync` если есть `"review_issues"` (server req §18).
- **Finalize:** НЕ требует `review_state=done` (server req §5). Warning — клиентская ответственность.
- **Baseline fingerprint test vectors:** захардкожены в server req §9.3, §9.4 (T1–T5, U1–U4).

### 5.4. Эндпоинты v0.3 (кратко)

7 новых эндпоинтов. Полный wire contract (method, path, auth, body, response, codes, idempotency) — **server req §6**:

```
POST   /api/v1/episodes/{episode_id}/review-issues
GET    /api/v1/episodes/{episode_id}/review-issues
PATCH  /api/v1/review-issues/{issue_id}
DELETE /api/v1/review-issues/{issue_id}
POST   /api/v1/review-issues/{issue_id}/comments
GET    /api/v1/review-issues/{issue_id}/comments
POST   /api/v1/episodes/{episode_id}/review-transition
```

### 5.5. line_ref — design spike (PENDING)

`line_ref` — opaque string на сервере (`minLength: 1`, `maxLength: 256`), сервер НЕ парсит внутреннюю структуру (server req §10). Формат identity — **design spike**, не принятое решение.

- **Локальный Phase 3** может использовать временный `line_ref` (interim identity, не покидает устройство).
- **Серверная персистенция ReviewIssue (Phase 6, multi-device sync)** начинается только после утверждения `line_ref` contract по результатам spike.
- Spike должен перебрать сценарии мутаций строки (retiming, EN-правки, split/merge, insert/delete, замена source). Варианты: hash EN, hash EN+timing, positional, UUID в ASS extradata, fuzzy+rebind, hash+context. Длительность: 1–2 недели (Phase 0.10).

Полная спецификация — server req §10.

### 5.6. Что НЕ входит в v0.3 (кратко)

- Server-side Diagnostics (вычисляются клиентски).
- WebSocket/streaming (`/sync` polling достаточно).
- Серверный ASS-парсер (нарушает opaque blob инвариант).
- Server-enforced RBAC (WontFix — client UX-policy, server req §3.3).
- Editable/deletable comments (immutable в v0.3, server req §7).
- Server-sync QCProfile и aliases (local-only, см. §5.8 ниже).
- Batch endpoints.

Полный список — server req §1.2.

### 5.7. Совместимость

| Client \ Server | v0.2 | v0.3 |
|---|---|---|
| **v0.2 client** | Полная v0.2 функциональность | v0.2 функциональность; v0.3 server возвращает новые поля — v0.2 client игнорирует |
| **v0.3 client** | **Degraded mode:** ReviewIssues/review_state локальные (sidecar); multi-device sync off; `server_capabilities` отсутствует в `/sync` → клиент остаётся в degraded | Полная функциональность; multi-device QC sync активен после `line_ref` spike |

Capability detection через `server_capabilities` в `/sync` (server req §18). Подробности — server req §17.

### 5.8. Local-only поля в V0.3: QCProfile и aliases (клиентская архитектура)

Эти настройки проекта остаются local-only в V0.3 (НЕ server-synced). Хранятся в **`SanaeLocalProjectConfig[project_id]`** — project-scoped local cache (НЕ per-file sidecar). Per-file sidecar `<file>.ass.aegisub.json` остаётся для line statuses, ReviewIssues и comments конкретного эпизода.

| Поле | Где хранится | Когда server-sync | Причина откладывания |
|---|---|---|---|
| `SanaeQCProfile` (пресеты + параметры проверок) | `SanaeLocalProjectConfig[project_id]` | V0.4 (additive поле в `ProjectBody`) | Конфигурация команды редко меняется; project-scoped, не дублируется по эпизодам |
| `aliases` на терминах | `SanaeLocalProjectConfig[project_id]` | V0.4 (additive поле в `TerminologyEntryBody`) | Aliases — V1.5 клиентская фича; project-scoped, переиспользуется всеми эпизодами |
| `AllowTranslatorWontFix` | Hardcoded `false` в клиенте (до V0.4) | V0.4 (часть server-sync QCProfile) | Если в локальных Preferences, переводчик может включить сам (см. 3.8) |
| `RequireDoneBeforeFinalize` | Hardcoded `false` в клиенте (до V0.4) | V0.4 (часть server-sync QCProfile) | Строгий режим блокировки Finalize — командное решение |

**Почему project-scoped, не per-file:** QCProfile и aliases — настройки всего проекта, а не отдельного эпизода. Хранение в per-file sidecar привело бы к дублированию (человек не должен заново добавлять «защищу / защитил / защитить» в каждой серии). `SanaeLocalProjectConfig[project_id]` хранится в `?user/sanae/local-config/<project-uuid>.json`.

**Когда V0.4 добавит server-sync для этих полей**, это будет аддитивное расширение: новые опциональные поля в `ProjectBody` и `TerminologyEntryBody`, новые массивы в `ProjectSyncResponse`. Обратно совместимо с v0.3 клиентами (игнорируют неизвестные поля).

**Для multi-device команд в V0.3:** QCProfile и aliases настраиваются на каждой машине один раз в `SanaeLocalProjectConfig[project_id]`. Это компромисс — не идеально, но допустимо для маленькой доверенной команды, пока server-sync не добавлен.

---

## 6. Поэтапный план внедрения

Принцип: instrumentation сначала, потом UX-выигрыши, потом глубокий рефакторинг. `LineContextPanel` поднимается раньше режимов — это главный новый UX-компонент, на нём проверяется вся философия интерфейса.

### Phase 0 — Instrumentation + audit (1 неделя)

| Шаг | Файлы | Эффект |
|---|---|---|
| 0.1 RAII-таймеры в `agi::log` (Debug) | новый `src/sanae_profiling.h` | Базовая инфраструктура |
| 0.2 Инструментировать `BaseGrid::OnPaint` | `base_grid.cpp:360-550` | Время paint |
| 0.3 Инструментировать `GenerateCandidates` | `sanae_project.cpp:2661-2863` | Полное время + Hunspell load |
| 0.4 Инструментировать `TerminologyConsistencyIssues` | `sanae_project.cpp:2865-2945` | Время + count |
| 0.5 Инструментировать `Finalize` (по фазам) | `sanae_project.cpp:3186-3288` | compact/sha/upload/merge/rebuild |
| 0.6 Инструментировать `OnActiveLineChanged`, `SetColumnWidths`, `RebuildUnits` | `base_grid.cpp`, `translation_project.cpp` | Время на смену строки |
| 0.7 `--profile` CLI-флаг | `src/main.cpp` | Включение логирования |
| 0.8 Запуск на самом тяжёлом реальном проекте команды | — | Реальный baseline |
| 0.9 Аудит `default_hotkey.json` на конфликты | `libresrc/default_hotkey.json` | Карта занятых комбинаций |
| 0.10 Design spike: `line_ref` stability | — | Отдельный отчёт (см. 5.7) |
| 0.11 UX-baseline: 2–5 переводчиков, типичные задачи | — | Количественный baseline (см. 8.1) |
| 0.12 Подтвердить внутреннее представление `line.Start`/`line.End` в Aegisub | `src/ass_dialogue.h`, `src/ass_time.h` | Каноническая `to_centiseconds()` конверсия (см. 3.6.1) |

### Phase 1 — Русификация и дешёвые UX fixes (1–2 недели)

| Шаг | Файлы | Эффект |
|---|---|---|
| 1.1 Fuzzy-фиксы в `po/ru.po` | `po/ru.po:6912, 6985` + аудит 61 fuzzy | Видимые баги локализации уходят |
| 1.2 Форматные строки вместо `wxString::Replace` | `dialog_sanae_final_review.cpp:80-88`, `sanae_project.cpp:2934-2939` | Локализация не ломается |
| 1.3 Debounce на фильтр терминологического диалога | `dialog_sanae_terminology.cpp:523` | Список не лагает |
| 1.4 `wxBusyCursor` в `FinalReviewDialog::Populate` | `dialog_sanae_final_review.cpp:374` | Видимая обратная связь |
| 1.5 Удаление `TerminologyCandidateDialog` (дубликат) | `dialog_sanae_terminology.cpp:106` | Один модальный диалог меньше |
| 1.6 Стандартизированный глоссарий UI-терминов | `po/ru.po` | Согласованность |

**Риск:** минимальный. **Тестирование:** `msgfmt --check-format --check-header po/ru.po` + существующие `tests/tests/sanae_*`.

### Phase 2 — LineContextPanel + Terminology V1 (2–3 недели)

**Цель:** главный новый UX-компонент переводчика + inline-терминология. Проверка философии интерфейса до переделки глобальной компоновки.

| Шаг | Файлы | Эффект |
|---|---|---|
| 2.1 `SanaeTerminologyIndex` (Aho-Corasick) | новый `src/sanae_terminology_index.h/.cpp` | O(text_length + matches) lookup |
| 2.2 Heavy/light split: Aho-Corasick на `ActiveLineChanged`, usage check на `wxEVT_STC_MODIFIED` | `sanae_project.h`, `subs_edit_box.cpp` | Без debounce, без лагов |
| 2.3 `MatchTerminologyForLine` + ranking (longer exact phrase > shorter exact phrase > exact single word; **без partial в V1**) | `sanae_project.h` | Top 3–5 релевантных |
| 2.4 Usage state: ✓ если exact RU match, **нейтрально** если нет (без auto-misused) | `sanae_terminology_index` | Нет ложных предупреждений |
| 2.5 `LineContextPanel` с приоритетным содержимым | новый `src/line_context_panel.h/.cpp` | Одна панель вместо стека |
| 2.6 `TerminologyHintPanel` как подсекция LineContextPanel | `terminology_hint_panel.h/.cpp` | Термины видны во время перевода |
| 2.7 Hotkeys `Alt+1..5`, `Ctrl+T`, `Ctrl+I` (после аудита Phase 0) | `libresrc/default_hotkey.json`, `command/terminology_inline.cpp` | Меньше кликов |
| 2.8 `TerminologyEntryPopover` (объединить `TermEditDialog` + `TerminologyEntryDialog`) | новый `src/terminology_entry_popover.h/.cpp` | Один поповер вместо двух диалогов |
| 2.9 `english_normalized` в `QueueTerminology*` (O(1) lookup) | `sanae_project.cpp:3023-3112` | Быстрее + консистентнее |
| 2.10 Optional aliases field в глоссарии (V1.5, не блокирующее) | `sanae_project.h`, `terminology_entry_popover` | Расширенный usage matching без morphology |
| 2.11 Измерение Relevance@3 / Apply rate / Irrelevant rate (KPI 8.3) | — | Подтверждение релевантности |

**Риск:** средний. **Митигация:** feature flag `Sanae/InlineTerminology`.

### Phase 3 — Diagnostic + ReviewIssue + QC dock + minimal WorkspaceMode (3–4 недели)

**Цель:** единый ProblemsList, локальные ReviewIssues, QC-док-панель, минимальный каркас режимов (нужен для QC hotkey context). (Серверная часть — Phase 6.)

**Важно:** минимальный `WorkspaceMode` переносится сюда из Phase 4, потому что Phase 3 уже требует контекст `Sanae QC` для hotkeys и workspace-sensitive actions в LineContextPanel. Полная перестройка layout остаётся в Phase 4.

| Шаг | Файлы | Эффект |
|---|---|---|
| 3.0 `WorkspaceMode` enum (минимальный: Translation/QC) + `SanaeUserRole` enum | новый `src/workspace_mode.h`, `src/sanae_user_role.h` | Каркас для QC context |
| 3.0.1 `SetWorkspaceMode` минимальный: show/hide `QCIssueDock` | `frame_main.cpp` | QC dock видим только в QC mode |
| 3.1 `SanaeDiagnostic` модель | новый `src/sanae_diagnostic.h/.cpp` | Transient вычисляемые проблемы |
| 3.2 `SanaeReviewIssue` модель + `SanaeComment` + `resolution_note` | новый `src/sanae_review_issue.h/.cpp` | Persistent человеческие замечания |
| 3.3 `SanaeIssueRegistry` агрегирует Diagnostic + ReviewIssue | новый `src/sanae_issue_registry.h/.cpp` | Единый источник для UI |
| 3.4 Обёртка `CheckLine` → `SanaeDiagnostic` | `translation_project.cpp:904-949` | Авто-проверки в едином списке |
| 3.5 Расширенные auto-QC проверки (empty, whitespace, dash, quotes, ellipsis, CPS low, line breaks, untranslated, punctuation, malformed tags) | `translation_project.cpp:904-949` + новый `src/sanae_qc_checks.h/.cpp` | Больше Diagnostics |
| 3.6 `SanaeQCProfile` с пресетами (TeamStandard/Strict/Minimal/Custom) | новый `src/sanae_qc_profile.h/.cpp` | Настраиваемость без UI-шума |
| 3.7 `QCIssueDock` (замена `FinalReviewDialog`) + контекстная навигация `F4` | новый `src/qc_issue_dock.h/.cpp` | Не модальный, кликабельный, с навигацией |
| 3.8 Команды QC + hotkeys (`F4`, `Q`, `Enter`, `Backspace`, `C`) в контексте `Sanae QC` | новый `src/command/qc.cpp` + `libresrc/default_hotkey.json` | Меньше мыши |
| 3.9 State machine `open/ready_for_review/resolved/wont_fix` + `resolution_note` | `sanae_review_issue.h` | Унифицировано клиент↔сервер |
| 3.10 `modified_after_issue` флаг с baseline fingerprints (canonical conversion, см. 3.6.1) | `sanae_review_issue.cpp` + подписка на `AssFile::COMMIT_DIAG_*` | Field-specific, вычисляемый |
| 3.11 WontFix UX-policy + client workflow role; no server RBAC (`AllowTranslatorWontFix` hardcoded false) | `sanae_review_issue.cpp`, `sanae_user_role.h` | Переводчик не может игнорировать QC |
| 3.12 `QuickIssuePopover` (с auto-типом по контексту) | новый `src/qc_quick_issue_popover.h/.cpp` | Быстрое создание замечания |
| 3.13 Persistent state: ReviewIssues + comments в per-file sidecar; QCProfile + aliases в project-scoped local cache (см. 5.10) | `translation_project.cpp:1088-1145`, новый `src/sanae_local_project_config.h/.cpp` | Issues/comments переживают рестарт; QCProfile/aliases не дублируются по эпизодам |
| 3.14 Визуальная иерархия (полоски слева в сетке) | `base_grid.cpp:443-514` | Спокойный интерфейс |
| 3.15 LineContextPanel workspace-sensitive actions (по `SanaeUserRole` + `WorkspaceMode`) | `line_context_panel.cpp` | Разные кнопки в режиме перевода vs QC |

**Риск:** высокий. **Митигация:** feature flag `Sanae/UnifiedProblemsList`. Fallback к старому `FinalReviewDialog` в расширенном режиме.

### Phase 4 — Workspace modes (full layout) + polish (2–3 недели)

**Цель:** полная перестройка layout — пресеты, сплиттеры, focus mode, оставшиеся док-панели. Минимальный каркас `WorkspaceMode` уже создан в Phase 3.

| Шаг | Файлы | Эффект |
|---|---|---|
| 4.1 `WorkspaceMode` расширен: добавить `Advanced` preset + полные пресеты layout | `src/workspace_mode.h`, `frame_main.h` | Переключение раскладки |
| 4.2 `SetWorkspaceMode` полная реализация: видимость панелей по preset | `frame_main.cpp:187-219` | Один клик/горячая клавиша |
| 4.3 `wxSplitterWindow` между видео и tools | `frame_main.cpp:204-211` | Гибкая граница |
| 4.4 EN-строка над RU-редактором (по умолчанию) | `subs_edit_box.cpp:200-218` | Источник под рукой |
| 4.5 Focus mode (`Ctrl+Shift+F`) | `frame_main.cpp` | Минимум визуального шума |
| 4.6 `TerminologyDock` (замена `TerminologyDialog`) | `dialog_sanae_terminology.cpp` → панель | Не модальный |
| 4.7 `ProjectNavigatorDock` (замена `ProjectDialog`) | `dialog_sanae_project.cpp:251` → панель | Не модальный |
| 4.8 `ProjectSearchDock` (замена `ProjectSearchDialog`) | `command/sanae.cpp:40` → панель | Не модальный |
| 4.9 `DiffDock` (замена `SemanticDiffDialog`) | `dialog_sanae_episode.cpp:118` → панель | Не модальный |
| 4.10 Workflow: Submit for QC (`Ctrl+Enter`, локальный триггер) ≠ Finalize | новый `src/command/episode_workflow.cpp` | Раздельные концепции |

**Риск:** средний — компоновка касается всех. **Митигация:** «Расширенный» режим = текущая раскладка, всегда доступен.

### Phase 5 — Производительность, подтверждённая Phase 0 (2 недели)

| Шаг | Файлы | Эффект |
|---|---|---|
| 5.1 `RefreshRect` вместо `Refresh(false)` в `OnActiveLineChanged` | `base_grid.cpp:278-293` | Меньше перерисовка |
| 5.2 Инкрементальный `RebuildUnits` | `translation_project.cpp:250-265` | O(1) вместо O(N) на addrem |
| 5.3 Кэш ширины колонок с инвалидацией по стилю | `base_grid.cpp:755-777`, `grid_column.cpp:244-246` | Меньше обходов events |
| 5.4 Background `GenerateCandidates`/`TerminologyConsistencyIssues` | `sanae_project.cpp` + `agi::dispatch::Background` | UI не блокируется |
| 5.5 Async `Finalize` + `wxProgressDialog` | `sanae_project.cpp:3186-3288` | UI не блокируется |
| 5.6 `pending_finalize_key` не сбрасывать на text-only commit без normalized change | `sanae_project.cpp:1885-1893` | Ретрай работает после правки опечатки |
| 5.7 Виртуализация списков кандидатов (`wxLC_VIRTUAL`) | `dialog_sanae_terminology.cpp:150-156` | 200+ строк без лага |
| 5.8 Измерение до/после каждой оптимизации | — | Подтверждение эффекта |

**Риск:** низкий — точечные оптимизации. **Митигация:** профилирование ДО и ПОСЛЕ каждой.

### Phase 6 — Server v0.3: ReviewIssue + episode review workflow + multi-device sync (3 недели)

**Цель:** multi-device QC-синхронизация. После `line_ref` spike (Phase 0.10).

| Шаг | Эффект |
|---|---|
| 6.1 `ReviewIssueBody` + `LineCommentBody` схемы (с `baseline_text_hash`/`baseline_timing_hash`) | Серверные сущности |
| 6.2 `EpisodeBody.review_state` + `review_state_version` поле | Episode workflow state |
| 6.3 6 эндпоинтов review-issues + 1 эндпоинт review-transition | CRUD + workflow |
| 6.4 Расширение `ProjectSyncResponse` (`review_issues`, `review_comments`) | Multi-device sync |
| 6.5 `line_ref` формат (по результатам spike) | Stable per-line identity |
| 6.6 Клиентский адаптер: `SanaeIssueRegistry` синхронизируется с сервером | Local-first, server-synced |
| 6.7 Episode review state: Submit for QC / Mark Done вызывают server transitions | Multi-device видимость workflow |
| 6.8 Fallback: v0.3 клиент + v0.2 сервер = локальный сайдкар (degraded) | Совместимость |

**Риск:** средний — серверные изменения требуют миграции БД. **Митигация:** additive schema, backward-compatible API.

### Общая длительность: ~14–18 недель

Phase 2 (LineContextPanel + Terminology) и Phase 5 (перформанс) независимы. Phase 6 (сервер) может идти параллельно с Phase 3 (клиентский QC, локальная часть) — после завершения `line_ref` spike.

### Feature-флаги

- `Sanae/InlineTerminology` (Фаза 2)
- `Sanae/LineContextPanel` (Фаза 2)
- `Sanae/UnifiedProblemsList` (Фаза 3) — Diagnostic + ReviewIssue в одном списке
- `Sanae/QCProfiles` (Фаза 3)
- `Sanae/ReviewIssues` (Фаза 3 локально, Фаза 6 серверно)
- `Sanae/WorkspaceModes` (Фаза 4)
- `Sanae/AsyncFinalize` (Фаза 5)
- `Sanae/ServerReviewSync` (Фаза 6, auto-detected)

По умолчанию — off для beta, on для следующего beta. Обратный откат — одна настройка.

---

## 7. Риски и совместимость

### 7.1. ASS-совместимость

Не нарушается. Все изменения — в UI-слое и в сайдкаре. Production ASS остаётся стандартным. `SanaeCompactStats` и `sanae_compact_rusub.cpp` не меняются.

### 7.2. Существующие проекты

Сайдкар `<file>.ass.aegisub.json` расширяется опциональными полями (`review_issues`, `comments`). QCProfile и aliases хранятся отдельно в `SanaeLocalProjectConfig[project_id]` (project-scoped, см. 5.10). Старые сайдкары читаются без изменений. Локальный Sanae-кэш расширяется опционально.

### 7.3. Sanae Server совместимость

v0.2 сервер продолжает работать с v0.3 клиентом (degraded: `ReviewIssue` и `review_state` локальные). v0.3 сервер обратно совместим с v0.2 клиентом (новые поля опциональны).

### 7.4. Производительность

Все тяжёлые операции — в `agi::dispatch::Background` с прогрессом. Inline-матчинг терминов: heavy один раз на строку, light на каждое нажатие без debounce.

### 7.5. Режимы без потери мощности

«Расширенный» режим = текущий Aegisub. Все профессиональные функции (стили, ASS-теги, тайминг, karaoke, vector clip) доступны. Опытный таймер/typesetter ничего не теряет.

### 7.6. line_ref неопределённость

До завершения spike (Phase 0.10) **серверная персистенция `ReviewIssue` не активируется** (см. 5.7). Локальный Phase 3 может использовать временный `line_ref` (interim identity, не покидает устройство). Multi-device sync включается только после утверждения `line_ref` contract. Это явный риск, но не блокирует локальную реализацию.

---

## 8. KPI и измерения

### 8.1. UX-baseline (Phase 0.11)

Перед ревампом — 2–5 переводчиков выполняют типичные задачи, записываем:

| Метрика | Как измеряем |
|---|---|
| Время на 20 строк перевода | Таймер от открытия эпизода до 20-й сохранённой строки |
| Сколько раз открывали модальные окна | Логирование `ShowModal()` вызовов |
| Сколько раз искали термин вручную | Логирование `sanae/project/terminology` команды |
| Кликов на QC issue | Логирование кликов в `FinalReviewDialog` |
| Сколько бесполезных подсказок | Субъективная оценка после сессии |

После Phases 2–3 — повторяем те же задачи, сравниваем.

### 8.2. Performance-метрики

Профилирование через `agi::log` (Debug) + `--profile` CLI-флаг. Целевые показатели **подтвердить в Phase 0** на реальном тяжёлом проекте:

| Операция | Ожидаемый диапазон | Цель после оптимизации |
|---|---|---|
| `GenerateCandidates` | ~2–5 с (UI freeze) | < 500 мс (background) |
| `TerminologyConsistencyIssues` | ~1–3 с (UI freeze) | < 200 мс (cached) |
| `BaseGrid::OnPaint` (30 rows) | ~5–10 мс | < 5 мс |
| `OnActiveLineChanged` | full `Refresh(false)` | `RefreshRect` 2 rows |
| `Finalize` end-to-end | ~3–8 с (UI freeze) | < 100 мс UI + background |
| Inline term match (heavy, line switch) | N/A | < 1 мс (Aho-Corasick) |
| Inline term usage check (light, keystroke) | N/A | < 0.1 мс |

### 8.3. Качественные метрики терминологии

Первоначальная проблема была «100+ совпадений». Успех — не просто быстрый поиск, а хорошая релевантность. Разделяем три метрики вместо одного «Precision@3»:

- **Relevance@3:** доля показанных top-3 терминов, которые переводчик считает относящимися к строке (по субъективной оценке после сессии или quick poll «этот термин был полезен?»). Это метрика «не шумно ли».
- **Apply rate:** доля показанных терминов, применённых через кнопку/hotkey (`Alt+1..5` или клик). Это метрика «полезно ли настолько, что использовали».
- **Irrelevant suggestion rate:** доля показанных терминов, которые переводчик счёл вообще не относящимися к строке. Это метрика ложных срабатываний.

**Целевые показатели (подтвердить в Phase 0.11 и Phase 2.11):**
- Relevance@3 ≥ 80% (большинство показанных терминов действительно релевантны)
- Apply rate не имеет жёсткой цели (переводчик может увидеть релевантный термин, запомнить его и написать правильную словоформу вручную — подсказка была полезной, хотя shortcut не использован)
- **Irrelevant suggestion rate < 5%** в top-3 — главная метрика качества релевантности

Прежняя цель «0 ложных терминологических предупреждений» технически тривиальна (V1 вообще не выдаёт warning при отсутствии RU match) и не измеряет реального качества. **<5% irrelevant в top-3** — содержательная цель.

**Другие качественные метрики:**
- Опрос переводчиков после Phase 2: «сколько времени вы тратите на поиск терминов за час перевода?»
- Опрос QC после Phase 3: «сколько замечаний вы оставляете за час ревью?»
- Usage логи: частота использования горячих клавиш vs меню.

### 8.4. UX-метрики (до/после)

| Метрика | Текущее | Цель |
|---|---|---|
| Кликов для применения термина | 5+ | 1 (`Alt+1`) |
| Кликов для создания QC-замечания | невозможно | 2 (`Q` + `Enter`) |
| Кликов от «готово» до «отправлено на QC» | 2–3 + UI freeze | 1 (`Ctrl+Enter`) |
| Модальных диалогов в типичной сессии перевода | 4–6 | 0–1 |
| Время блокировки UI на Final Review | секунды | < 200 мс (incremental) |
| Время блокировки UI на Finalize | секунды | 0 (async) |
| Ложных терминологических предупреждений | N/A | <5% irrelevant в top-3 (см. 8.3) |

---

## 9. Итог

Этот план:

1. **Сохраняет все профессиональные функции** — режимы не удаляют, а скрывают за progressive disclosure.
2. **Делает терминологию видимой во время перевода** через Aho-Corasick + heavy/light split, **без auto-misused** до aliases/morphology.
3. **Разделяет модели хранения** Diagnostic (transient) и ReviewIssue (persistent), объединяя только на уровне отображения.
4. **Унифицирует state machine** `open/ready_for_review/resolved/wont_fix` с `modified_after_issue` как вычисляемым флагом, синхронизируемым через baseline fingerprints.
5. **Решает multi-device QC** через серверные `ReviewIssue` + `episode.review_state` + baseline fingerprints.
6. **Убирает ручные статусы** там, где состояние выводится из действий; «Готово к проверке» — явное человеческое действие.
7. **Разделяет Submit for QC и Finalize** — разные концепции, разные действия.
8. **Вводит QC-профили с пресетами** — настраиваемость без UI-шума для 95% переводчиков.
9. **Ограничивает WontFix** ролью QC (переводчик может только «Не согласен» + комментарий).
10. **Единая панель «Контекст строки»** с приоритетным содержимым и workspace-sensitive actions.
11. **Phase 0 — instrumentation + UX-baseline** до любых оптимизаций.
12. **`line_ref` — design spike**, не принятое решение.
13. **Локализует интерфейс** — fuzzy-фиксы, форматные строки, стандартизированный глоссарий.
14. **Внедряется по фазам** с feature-флагами и откатами.

Каждое изменение обосновано конкретной цитатой кода. План реалистичен для постепенной реализации без нарушения ASS-совместимости и совместимости с Sanae Server v0.2.

---

## Приложение A — Список затрагиваемых файлов

### Новые файлы

- `src/sanae_diagnostic.h/.cpp` — transient Diagnostics
- `src/sanae_review_issue.h/.cpp` — persistent ReviewIssues + Comments
- `src/sanae_issue_registry.h/.cpp` — агрегатор Diagnostic + ReviewIssue
- `src/sanae_terminology_index.h/.cpp` — Aho-Corasick + кэш
- `src/sanae_qc_checks.h/.cpp` — расширенные auto-проверки
- `src/sanae_qc_profile.h/.cpp` — профиль + пресеты
- `src/sanae_status_machine.h/.cpp` — авто-переходы состояний
- `src/sanae_profiling.h` — RAII-таймеры
- `src/sanae_visual_hierarchy.h` — правила визуальной иерархии
- `src/line_context_panel.h/.cpp` — приоритетная панель контекста
- `src/terminology_hint_panel.h/.cpp` — подсекция терминов в LineContextPanel
- `src/terminology_entry_popover.h/.cpp` — объединённый popover
- `src/qc_issue_dock.h/.cpp` — док-панель QC
- `src/qc_quick_issue_popover.h/.cpp` — быстрое создание замечания
- `src/workspace_mode.h` — enum режимов
- `src/command/qc.cpp` — QC-команды
- `src/command/terminology_inline.cpp` — inline-команды терминов
- `src/command/episode_workflow.cpp` — Submit for QC / Mark Done

### Изменяемые файлы

- `src/frame_main.h/.cpp` — режимы, сплиттер, док-панели
- `src/base_grid.cpp` — RefreshRect, визуальная иерархия, профилирование
- `src/subs_edit_box.cpp` — EN-строка, LineContextPanel, popover
- `src/sanae_project.h/.cpp` — `MatchTerminologyForLine`, async `Finalize`, `english_normalized` в Queue*, `pending_finalize_key` фикс
- `src/dialog_sanae_final_review.cpp` — deprecated → опциональный fallback
- `src/dialog_sanae_terminology.cpp` — debounce фильтра, удаление `TerminologyCandidateDialog`, переход к док-панели
- `src/dialog_sanae_episode.cpp` — `EpisodeDetailsDialog` → частично инлайн
- `src/dialog_sanae_project.cpp` — `ProjectDialog` → док-навигатор
- `src/dialog_translation.cpp` — авто-статусы, интеграция с LineContextPanel
- `src/translation_project.h/.cpp` — Diagnostic-обёртки, расширенные проверки, инкрементальный `RebuildUnits`, авто-переходы
- `src/command/sanae.cpp` — команды вызывают новые панели вместо диалогов
- `src/grid_column.cpp` — уровни отображения QC
- `libresrc/default_hotkey.json` — новые горячие клавиши (после аудита)
- `libresrc/default_menu.json` — новые пункты меню
- `po/ru.po` — fuzzy-фиксы, глоссарий, новые строки

### Серверные (отдельный репозиторий)

- `openapi.yaml` — 3 новые схемы (`ReviewIssueBody`, `LineCommentBody`, расширенный `EpisodeBody`), 7 новых эндпоинтов, расширение `ProjectSyncResponse`
- Миграция БД: таблицы `review_issues`, `review_comments`; поле `review_state` на `episodes`

---

## Приложение B — Неизменяемые сущности

- `AssDialogue`, `AssFile`, `AssStyle`, `AssAttachment`, `AssOverride` — ASS-структуры без изменений.
- `SanaeApiClient` (`sanae_api.h`) — thin transport, без изменений.
- `SanaeCompactStats`, `sanae_compact_rusub.cpp` — compact constructor без изменений.
- `sanae_batch_import.cpp` — batch flow без изменений (остаётся wizard).
- `sanae_recovery.cpp` — recovery без изменений (snapshots остаются opaque blobs).
- `sanae_subtitle_diff.cpp` — semantic diff без изменений (используется в `DiffDock`).
- `sanae_text.cpp` — normalization без изменений (используется во всех новых индексах).

---

## Changelog

- **v2.1.1-final-sync** (текущий, заморожен): Синхронизация клиентского плана с server requirements. `SanaeReviewIssue` struct: `base_version` → `version` (persisted current version; `base_version` — client-supplied PATCH precondition, не persisted). §5 полностью переписан как **краткий обзор со ссылкой на `SANAE_SERVER_REQUIREMENTS_v0.3.md` как authoritative** для wire/API — устранено дублирование schemas/endpoints, которое вызывало расхождения. Reopen behavior (`resolved_at`/`resolved_by_device_id`/`resolution_note` cleared) сделано normative в §3.5 (не только в changelog/server req). Все остальные решения v2.1.1-final сохранены.
- **v2.1.1-final**: Финальные контрактные cleanup. Удалены `edited_at`/`deleted_at` из клиентской `SanaeComment` (immutable, соответствует server §7); `base_version` → `base_review_state_version` в episode review-transition; конвенции уточнены (`base_version` для ReviewIssue, `base_review_state_version` для episode); раздел 5.10 переписан — QCProfile/aliases в `SanaeLocalProjectConfig[project_id]` (project-scoped), НЕ per-file sidecar; `Mark Done` invariant игнорирует soft-deleted issues (`state IN (...) AND deleted_at IS NULL`); `done → POST /review-issues` запрещён сервером (`409 episode_review_done`); capability detection через `server_capabilities` в `/sync` (не probing); resolved_at/resolved_by очищаются на Reopen (исторический audit — в ProjectChangeBody); idempotency fingerprint = method + path + body (не просто body hash); test vectors baseline fingerprints захардкожены. UX-архитектура заморожена, Phase 0 готов к запуску.
- **v2.1.1**: Точечные контрактные cleanup без переделки UX. Добавлены: `resolution_note` как отдельное поле (не `body`) для `wont_fix`; `SanaeUserRole` enum (client workflow role, не RBAC); исправлена циклическая зависимость Phase 3/4 (минимальный `WorkspaceMode` перенесён в Phase 3); исправлена ranking-строка в Phase 2 checklist (убран `partial`); исправлен WontFix в Phase 3 checklist (UX-policy + role, не role-based authorization); `QCPassed` invariant исправлен (нет open **или** ready_for_review); контекстная навигация `F4` (Translator → Open/blocking, Reviewer → ReadyForReview → Open); фильтр «мои» убран (нет user identity); local-only QCProfile/aliases перенесены в project-scoped cache (`SanaeLocalProjectConfig[project_id]`), не per-file sidecar; `base_version` naming: `review_state_version`/`base_review_state_version` для episode, `version`/`base_version` для ReviewIssue; canonical conversion для timing hash (`to_centiseconds()`, не сырой internal int) с пометкой подтвердить в Phase 0.12; раздел 7.6 приведён в соответствие с 5.7.
- **v2.1**: Контрактные исправления без переделки UX-архитектуры. Добавлены: transition matrix для `ReviewIssue` (3.5) и `episode.review_state` (3.9.1) с серверными invariantами; канонический алгоритм хеширования baseline fingerprints (3.6.1 — UTF-8/ASCII, centiseconds, без NFKC, `\N` сохраняется, override-блоки удаляются); immutable comments в V0.3 (5.3 — убраны `edited_at`/`deleted_at`, убраны PATCH/DELETE на comments); явное «Finalize НЕ требует review_state=done» с optional warning; QCProfile и aliases явно local-only в V0.3 (5.10); WontFix переименован в UX-policy (не server-enforced authorization), `AllowTranslatorWontFix` НЕ в локальных Preferences; упрощён ranking V1 (убран `partial`, осталось `longer exact phrase > shorter exact phrase > exact single word`); KPI терминологии разделён на Relevance@3 / Apply rate / Irrelevant suggestion rate с целью <5% irrelevant; исправлены редакционные ошибки (количество entity_type, формулировка line_ref spike vs Phase 6).
- **v2.0**: Чистая версия. Удалены противоречия v1.0/v1.1. Добавлены: серверный `episode.review_state` + endpoint; `baseline_text_hash`/`baseline_timing_hash` на `ReviewIssueBody` для multi-device `modified_after_issue`; удалён auto-misused в терминологии V1 (появится с aliases/morphology в V1.5/V2); QC-профили с пресетами; WontFix role-based; LineContextPanel workspace-sensitive actions; UX-baseline в Phase 0; precision@3 KPI; фазы переупорядочены (LineContextPanel раньше режимов).
- **v1.1** (deprecated): Исправления из design review: Diagnostic ≠ ReviewIssue, упрощённая state machine, `line_id` как spike, hotkeys `Alt+1..5`, Aho-Corasick V1, heavy/light split, QC-профили, Submit ≠ Finalize, profiling в Phase 0, единая LineContextPanel.
- **v1.0** (deprecated): Первоначальный документ. Содержал: единый `SanaeIssue` (неверно — смешаны transient и persistent); `FixedByTranslator` как состояние (неверно — должно быть флагом); `1…5` hotkeys в edit box (неверно — конфликт с вводом); debounce 200ms на терминологию (неверно — heavy/light split делает его ненужным); `line_id` как хеш EN+timing (placeholder, не решение); Submit for QC = Finalize (неверно — разные концепции); profiling в Phase 5 (неверно — должен быть в Phase 0).

---

**Конец документа (Rev 2.1.1-final-sync, заморожен).**
