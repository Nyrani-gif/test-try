# Sanae v0.3 — Source Repeat / Project Search matching2

## Итоговая модель

Автоматический поиск прошлых ENSUB теперь использует пять последовательных уровней:

1. Exact visible text — строгий нормализованный полный текст.
2. Exact fragment / containment — длинная непрерывная последовательность токенов.
3. Lexical near-repeat — token-index retrieval + Unicode/token-aware phrase similarity.
4. Multi-line split/merge — короткие spans из 1–3 соседних строк, включая 1↔2, 1↔3, 2↔1, 2↔2 и 3↔1.
5. Context-aware reranking — предыдущая/следующая EN-реплика выбирает лучший historical occurrence только среди уже валидных кандидатов.

## Производительность

- Exact: hash index.
- Fragment: 5/6-token window index.
- Similar: inverted token index; сравнивается не весь corpus, а до 160 retrieval candidates, обычно существенно меньше.
- Historical spans строятся один раз в RebuildMemory и не пересекают Episode или паузы > 4.5 секунды.
- Максимум 3 соседних строки / 48 токенов на span.
- Для span fuzzy retrieval нужен более сильный token overlap (2–3 редких токена).
- Context даёт только слабый rerank bonus до 0.035 и не может создать match самостоятельно.

## Safety guards

High-confidence Similar (default 0.92) не допускается при изменении polarity/negation или числового payload. Такие строки могут оставаться доступными при более низком ручном threshold, но не считаются безопасной near-repeat подсказкой по умолчанию.

## Project Search

Поиск по всему проекту теперь также рассматривает соседние 2–3 строки. Span-result выводится только если запрос не найден не хуже в одной из составляющих строк, поэтому обычные результаты не дублируются.

## UI

Новый тип называется `Другое разбиение реплики`. Он всегда reference-only: прошлый перевод не переносится автоматически. В Final Review один span создаёт один issue, даже если затрагивает несколько текущих строк.

## Проверка

В Linux-контейнере пройден отдельный C++20 text smoke с `-Wall -Wextra -Wpedantic -Werror` для Exact normalization, Fragment, word-move Similar, negation guard, numeric guard и split/merge normalization. Полный Windows/MSVC/wxWidgets build должен быть подтверждён CI.
