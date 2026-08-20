# Sanae Server Requirements v0.3

**Версия:** 0.3.0
**Назначение:** Контракт между клиентом Sanae и серверной частью. Документ самодостаточен — server coding agent не должен читать UX-план и не должен угадывать требования клиента.
**Основание:** `SANAE_REVAMP_PLAN.md` v2.1.1 + `sanae-server-openapi-v0.2.json` (фактический контракт v0.2).
**Принцип:** Только то, что клиент реально требует. Если чего-то недостаточно для однозначной реализации — помечено `OPEN CONTRACT QUESTION`, а не придумывается молча.

---

## 1. Scope v0.3 / Non-goals

### 1.1. Входит в v0.3 (сервер обязан реализовать)

- **ReviewIssue** — persistent человеческие замечания с state machine, baseline fingerprints, soft-delete.
- **LineComment** — immutable append-only комментарии к ReviewIssue.
- **Episode review workflow** — поле `review_state` на `EpisodeBody` + endpoint для переходов с server-enforced invariantами.
- **Sync integration** — `ReviewIssue`, `LineComment`, `episode.review_state` включены в `ProjectSyncResponse`.
- **Audit log** — новые `entity_type` значения в `ProjectChangeBody`.

### 1.2. НЕ входит в v0.3 (сознательно отложено)

| Функция | Причина | Когда |
|---|---|---|
| Server-side Diagnostics | Вычисляются клиентски (ASS-парсер, видео-контекст) | Never — архитектурное решение |
| WebSocket / streaming | `/sync` polling достаточно | v0.4+ если потребуется |
| Серверный ASS-парсер | Нарушает «opaque blob» инвариант v0.2 | Never |
| Полноценный RBAC | Devices уже principals; WontFix — client UX-policy | v0.4+ spike |
| Editable/deletable comments | Immutable в v0.3 — audit trail | v0.4 (additive) |
| Server-sync QCProfile | Local-only (см. §16) | v0.4 (additive поле в `ProjectBody`) |
| Server-sync aliases | Local-only (см. §16) | v0.4 (additive поле в `TerminologyEntryBody`) |
| Batch endpoints | Всё single-resource | v0.4+ если потребуется |
| `line_ref` format contract | Design spike pending (см. §10) | После spike, до Phase 6 |

### 1.3. Неизменяемые части v0.2

Следующие части контракта v0.2 **остаются без изменений**:
- Все 22 существующих эндпоинта.
- `EnrollRequest`/`EnrollResponse`, `DeviceBody`, `SeasonBody`, `ProjectBody`, `EpisodeBody` (только additive расширения), `EpisodeFileBody`, `FinalizedRevisionBody`, `RecoverySnapshotBody`, `TerminologyEntryBody`, `TerminologyHistoryBody`, `IgnoredCandidateBody`.
- `HTTPBearer` auth, `Idempotency-Key` header, `APIErrorEnvelope`.
- `/finalize` — без новых preconditions (см. §5).
- Файловые блобы — opaque, SHA-256 content addressing.

---

## 2. Data Model

### 2.1. ReviewIssueBody (НОВАЯ сущность)

```yaml
ReviewIssueBody:
  type: object
  required:
    - id
    - project_id
    - episode_id
    - line_ref
    - issue_type
    - severity
    - state
    - body
    - resolution_note
    - version
    - baseline_text_hash
    - baseline_timing_hash
    - created_by_device_id
    - created_at
    - updated_at
  properties:
    id:
      type: string
      format: uuid
      description: Server-generated UUID.
    project_id:
      type: string
      format: uuid
      description: Parent project. Must match episode.project_id.
    episode_id:
      type: string
      format: uuid
      description: Parent episode.
    line_ref:
      type: string
      minLength: 1
      maxLength: 256
      description: >
        Stable line identity. FORMAT PENDING — see §10 (line_ref contract gate).
        Server treats this as opaque string in v0.3.
        Server does NOT parse or validate the internal structure.
        Server MUST reject empty strings (422).
    issue_type:
      type: string
      enum: [translation, terminology, timing, style, formatting, other]
      description: Category of the issue.
    severity:
      type: string
      enum: [info, warning, error]
      description: Severity level. 'error' = blocking (may prevent Mark Done, see §4.2).
    state:
      type: string
      enum: [open, ready_for_review, resolved, wont_fix]
      description: Current workflow state. See §3 for transition matrix.
    body:
      type: string
      nullable: true
      maxLength: 4096
      description: >
        Original description by creator ("what is wrong").
        Nullable (creator may leave empty if issue_type is self-explanatory).
        NOT used as wont_fix reason — see resolution_note.
    resolution_note:
      type: string
      nullable: true
      maxLength: 4096
      description: >
        Explanation of why the issue was decided not to be fixed ("why wont_fix").
        REQUIRED (non-empty) when state=wont_fix — server rejects PATCH with 422 otherwise.
        MUST be null in all other states (open, ready_for_review, resolved).
        Reset to null on Reopen from wont_fix (state → open).
        See §3.2 for enforcement rules.
    version:
      type: integer
      minimum: 1
      description: >
        Current version of this entity (server-side monotonic counter).
        Incremented on every successful PATCH.
        Used by client as base_version in next PATCH precondition.
        Distinct from base_version (which is a client-supplied precondition, not persisted state).
    baseline_text_hash:
      type: string
      pattern: '^[0-9a-f]{64}$'
      description: >
        SHA-256 hex (lowercase) of the line's visible text at issue creation.
        Enables any client to compute modified_after_issue.
        See §9 (Baseline Fingerprint Spec) for canonical algorithm.
        Immutable after creation (server rejects PATCH attempts to change it).
    baseline_timing_hash:
      type: string
      pattern: '^[0-9a-f]{64}$'
      description: >
        SHA-256 hex (lowercase) of the line's timing at issue creation.
        See §9. Immutable after creation.
    created_by_device_id:
      type: string
      format: uuid
      description: Device that created the issue. NOT a user identity — see §14.
    created_at:
      type: string
      format: date-time
    updated_at:
      type: string
      format: date-time
      description: Updated on every PATCH (state transition, body edit, resolution_note set).
    resolved_at:
      type: string
      format: date-time
      nullable: true
      description: Set when state transitions to resolved. CLEARED on Reopen (resolved/wont_fix → open). See §3.2 for semantics.
    resolved_by_device_id:
      type: string
      format: uuid
      nullable: true
      description: Device that transitioned to resolved. CLEARED on Reopen. See §3.2.
    deleted_at:
      type: string
      format: date-time
      nullable: true
      description: >
        Soft-delete timestamp. See §13 (Deletion semantics).
        Deleted issues appear in /sync with deleted_at set, but not in default GET list.
```

**Wire contract для optimistic concurrency:**
- Серверная сущность содержит `version` (текущая, в response).
- PATCH request содержит `base_version` (precondition, не persisted).
- Сервер отвергает PATCH если `base_version != current version` → `409`.

### 2.2. LineCommentBody (НОВАЯ сущность, immutable)

```yaml
LineCommentBody:
  type: object
  required: [id, issue_id, body, created_by_device_id, created_at]
  properties:
    id:
      type: string
      format: uuid
      description: Server-generated UUID.
    issue_id:
      type: string
      format: uuid
      description: Parent ReviewIssue.
    body:
      type: string
      minLength: 1
      maxLength: 4096
      description: Comment text. Non-empty (server rejects empty with 422).
    created_by_device_id:
      type: string
      format: uuid
    created_at:
      type: string
      format: date-time
```

**Immutable contract (см. §7):**
- НЕТ `edited_at`.
- НЕТ `deleted_at`.
- НЕТ `PATCH /comments/{id}`.
- НЕТ `DELETE /comments/{id}`.
- Ошибся — написал следующий комментарий. V0.4 может добавить edit/delete как additive расширение.

### 2.3. EpisodeBody — additive расширения

К существующей `EpisodeBody` (v0.2) добавляются поля:

```yaml
EpisodeBody:
  ...  # все существующие v0.2 поля без изменений
  properties:
    ...
    review_state:
      type: string
      enum: [translating, in_review, done]
      default: translating
      description: >
        Workflow state for human review. Distinct from `status`
        (translating/finalized/archived) which tracks file lifecycle.
        `review_state` tracks translator↔QC handoff.
        Derived display states (needs_fixes, re_review) are computed client-side
        from ReviewIssue states, NOT stored on server.
        Default 'translating' for all existing episodes on migration.
    review_state_version:
      type: integer
      minimum: 1
      description: >
        Current version of review_state (server-side monotonic counter).
        Incremented on every successful review-transition.
        Used by client as base_review_state_version in next transition precondition.
    review_state_updated_at:
      type: string
      format: date-time
      nullable: true
    review_state_updated_by_device_id:
      type: string
      format: uuid
      nullable: true
```

**Wire contract:**
- `review_state_version` — persisted state (в response).
- `base_review_state_version` — client-supplied precondition (в review-transition request).
- Сервер отвергает transition если `base_review_state_version != current review_state_version` → `409`.

### 2.4. ProjectSyncResponse — additive расширения

```yaml
ProjectSyncResponse:
  ...  # все существующие v0.2 поля без изменений
  properties:
    ...
    review_issues:
      type: array
      items: { $ref: "#/components/schemas/ReviewIssueBody" }
      description: >
        Touched (delta) or all (full_snapshot) review issues for the project.
        Includes soft-deleted issues (deleted_at set) for tombstone propagation.
        See §11 (Sync semantics).
    review_comments:
      type: array
      items: { $ref: "#/components/schemas/LineCommentBody" }
      description: >
        Touched or all comments. Comments are immutable (no tombstones needed).
```

### 2.5. ProjectChangeBody — новые entity_type значения

`ProjectChangeBody.entity_type` (v0.2: free-form string) получает три новых значения:

| entity_type | entity_id points to | operation values |
|---|---|---|
| `"review_issue"` | ReviewIssue.id | `"create"`, `"update"`, `"delete"` |
| `"review_comment"` | LineComment.id | `"create"` (immutable, no update/delete) |
| `"episode_review_state"` | Episode.id | `"transition"` |

`ProjectChangeBody` не хранит `operation` как enum в v0.2 (это free-form string), но сервер SHOULD использовать значения выше для консистентности.

---

## 3. State Machine — ReviewIssue

### 3.1. Transition matrix

| Из \ В | open | ready_for_review | resolved | wont_fix |
|---|---|---|---|---|
| **open** | — | ✓ | ✓ | ✓ (requires `resolution_note`) |
| **ready_for_review** | ✓ | — | ✓ | ✓ (requires `resolution_note`) |
| **resolved** | ✓ (Reopen) | ✗ | — | ✗ |
| **wont_fix** | ✓ (Reopen) | ✗ | ✗ | — |

### 3.2. Server-enforced invariants

| Invariant | Enforcement |
|---|---|
| `* → wont_fix` requires non-empty `resolution_note` | Server rejects PATCH with `422` if `state=wont_fix` AND (`resolution_note` is null OR empty string) |
| `resolution_note` MUST be null when `state != wont_fix` | Server rejects PATCH with `422` if `state in {open, ready_for_review, resolved}` AND `resolution_note` is non-null non-empty. Server auto-clears `resolution_note` to null on transitions into non-wont_fix states. |
| `baseline_text_hash` / `baseline_timing_hash` immutable after creation | Server rejects PATCH attempting to change these fields with `422` |
| `version` monotonic | Server increments `version` on every successful PATCH. Client-supplied `version` in PATCH is ignored (only `base_version` is used as precondition) |
| `resolved_at` / `resolved_by_device_id` set on transition to resolved | Server sets these on `* → resolved`. CLEARED on Reopen (`resolved/wont_fix → open`): `resolved_at = null`, `resolved_by_device_id = null`. На следующем Resolve выставляются заново. Семантика: `resolved_at` означает состояние **текущего** закрытия, не исторического. Исторический audit живёт в `ProjectChangeBody` (entity_type="review_issue", operation="update"), не в одном поле сущности. |
| No automatic transitions | Server does NOT compute transitions from timestamps, issue age, or any derived state. All transitions are explicit PATCH from client. |

### 3.3. Client UX-policy (NOT server-enforced)

Следующие ограничения — client-side UX-policy, **сервер НЕ enforce-ит**:

- «Only Reviewer role can WontFix» — сервер не знает ролей. Любое устройство может PATCH `state=wont_fix` (если `resolution_note` предоставлен).
- «Only Reviewer can Reopen» — сервер не знает ролей.
- «Translator cannot WontFix» — client-side `AllowTranslatorWontFix` flag, hardcoded `false`.

Сервер НЕ должен добавлять RBAC checks для этих операций в v0.3. Это сознательное решение для доверенной маленькой команды.

### 3.4. No server-side computation of display states

Сервер хранит только `state` (4 значения). Display states `needs_fixes` и `re_review` — **клиентские вычисления** из агрегированного состояния ReviewIssues эпизода:

- `review_state==in_review` + есть `open` ReviewIssue → client показывает «Needs Fixes»
- `review_state==in_review` + есть `ready_for_review` ReviewIssue → client показывает «Re-review»

Сервер НЕ хранит и НЕ возвращает эти display states.

---

## 4. State Machine — Episode review_state

### 4.1. Transition matrix

| Из \ В | translating | in_review | done |
|---|---|---|---|
| **translating** | — | ✓ (Submit for QC) | ✗ (forbidden — must go through in_review) |
| **in_review** | ✓ (Return to translator) | — | ✓ (Mark Done — requires no open/ready_for_review ReviewIssues) |
| **done** | ✗ (forbidden) | ✓ (Reopen for QC) | — |

### 4.2. Server-enforced invariants

| Transition | Precondition | Error if violated |
|---|---|---|
| `translating → in_review` | None (always allowed) | — |
| `in_review → translating` | None (always allowed — cancel accidental submit) | — |
| `in_review → done` | Episode has NO **non-deleted** ReviewIssue with `state in {open, ready_for_review}`. Literal SQL: `state IN ('open','ready_for_review') AND deleted_at IS NULL`. Soft-deleted issues (с `deleted_at`) НЕ блокируют done, независимо от их state. | `409 Conflict` with `error.code = "open_issues_exist"` |
| `done → in_review` | None (always allowed — reopen for additional QC) | — |
| `translating → done` | Forbidden (no direct path) | `422 Unprocessable Entity` with `error.code = "invalid_transition"` |
| `done → translating` | Forbidden (use `done → in_review` instead) | `422 Unprocessable Entity` with `error.code = "invalid_transition"` |

**Дополнительный invariant (сильный):** `review_state=done` запрещает создание новых ReviewIssues. `POST /episodes/{id}/review-issues` (§6.1) отвергает с `409 Conflict`, `error.code = "episode_review_done"`, если `episode.review_state == done`. Клиент сначала должен перевести `done → in_review`, затем создавать issue. Это сохраняет сильный invariant: `done` действительно означает отсутствие незакрытых ReviewIssues. Сервер проверяет `review_state` atomically с созданием issue (транзакция).

Сервер проверяет `in_review → done` invariant, запрашивая ReviewIssues эпизода. Это единственный transition с data-dependent precondition.

### 4.3. Effect on version/revision

- Каждый успешный `review-transition` инкрементирует `review_state_version` (optimistic concurrency).
- Каждый успешный `review-transition` инкрементирует `project_revision` (как и все мутации в v0.2).
- `ProjectChangeBody` запись: `entity_type="episode_review_state"`, `entity_id=episode.id`, `operation="transition"`.

### 4.4. Idempotency

`POST /episodes/{id}/review-transition` требует `Idempotency-Key` (UUID). Повтор с тем же key + тем же `base_review_state_version` возвращает тот же response. Повтор с тем же key + другим `base_review_state_version` → `422` (idempotency conflict).

---

## 5. Finalize contract

### 5.1. No new preconditions

`POST /episodes/{id}/finalize` (v0.2) **НЕ получает новых обязательных preconditions** в v0.3. В частности:

- `/finalize` НЕ требует `review_state=done`.
- `/finalize` НЕ проверяет отсутствие open ReviewIssues.
- `/finalize` НЕ требует `base_review_state_version`.

### 5.2. Rationale

Иногда нужно финализировать техническую ревизию (обновить compact RUSUB после исправления таймингов) без полного прохождения QC-цикла. Warning о незавершённом QC — клиентская ответственность, не серверная.

### 5.3. Server behavior

`/finalize` работает ровно как в v0.2. Единственное изменение: после успешного finalize, если `review_state` был не `done`, сервер НЕ меняет его автоматически. `review_state` остаётся как есть (обычно `in_review` или `done`). `EpisodeBody.status` становится `finalized` (v0.2 behavior).

Клиент MAY показать warning перед вызовом `/finalize` если `review_state != done`, но это исключительно client-side.

---

## 6. Endpoints

Все endpoints используют:
- `Authorization: Bearer <device_token>` (v0.2 auth).
- `Idempotency-Key: <uuid>` header для mutations (включая review-transition, comment create).
- `APIErrorEnvelope` для ошибок (v0.2 format).
- `base_version` / `base_review_state_version` для optimistic concurrency в PATCH/transition.

### 6.1. POST /api/v1/episodes/{episode_id}/review-issues

**Создание ReviewIssue.**

| | |
|---|---|
| Auth | Bearer required |
| Idempotency-Key | Required (UUID) |
| Path params | `episode_id` (uuid) |
| Request body | `ReviewIssueCreateRequest` (см. ниже) |
| Success | `201 Created` → `ReviewIssueCreateResponse` |
| Errors | 400, 401, 404 (episode not found), 413 (body/resolution_note too long), 422 (validation), 500 |

```yaml
ReviewIssueCreateRequest:
  type: object
  required: [line_ref, issue_type, severity, baseline_text_hash, baseline_timing_hash]
  properties:
    line_ref: { type: string, minLength: 1, maxLength: 256 }
    issue_type: { type: string, enum: [translation, terminology, timing, style, formatting, other] }
    severity: { type: string, enum: [info, warning, error] }
    body: { type: string, nullable: true, maxLength: 4096 }
    # state defaults to "open" on creation — client cannot set state in create request
    # resolution_note not allowed in create (state is always "open")
    baseline_text_hash: { type: string, pattern: '^[0-9a-f]{64}$' }
    baseline_timing_hash: { type: string, pattern: '^[0-9a-f]{64}$' }

ReviewIssueCreateResponse:
  type: object
  required: [issue, project_revision]
  properties:
    issue: { $ref: "#/components/schemas/ReviewIssueBody" }
    project_revision: { type: integer, minimum: 1 }
```

**Server behavior:**
- Проверяет `base_version == current version`. Если нет → `409` с `error.code = "version_conflict"`.
- Проверяет `episode.review_state != done`. Если `done` → `409` с `error.code = "episode_review_done"` (см. §4.2 — сильный invariant).
- `state` = `"open"` (forced, client cannot override).
- `resolution_note` = `null` (forced, since state != wont_fix).
- `version` = `1`.
- `resolved_at`, `resolved_by_device_id`, `deleted_at` = `null`.
- `created_by_device_id` = текущее device (из token).
- `created_at`, `updated_at` = server time.
- Инкрементирует `project_revision`, пишет `ProjectChangeBody` (`entity_type="review_issue"`, `operation="create"`).

### 6.2. GET /api/v1/episodes/{episode_id}/review-issues

**List ReviewIssues для эпизода.**

| | |
|---|---|
| Auth | Bearer required |
| Idempotency-Key | Not required (GET) |
| Path params | `episode_id` (uuid) |
| Query params | `state` (optional, comma-separated, e.g. `?state=open,ready_for_review`), `severity` (optional, comma-separated), `include_deleted` (optional, default `false`) |
| Success | `200 OK` → `ReviewIssueListResponse` |
| Errors | 400, 401, 404, 500 |

```yaml
ReviewIssueListResponse:
  type: object
  required: [episode_id, issues]
  properties:
    episode_id: { type: string, format: uuid }
    issues:
      type: array
      items: { $ref: "#/components/schemas/ReviewIssueBody" }
```

**Pagination:** V0.3 не вводит pagination. Эпизод обычно содержит < 100 issues. Если pagination потребуется — v0.4 (additive `?limit=`, `?offset=`, `?cursor=`).

**Default behavior:** `include_deleted=false` → исключает issues с `deleted_at != null`. `include_deleted=true` → включает их (для audit UI).

### 6.3. PATCH /api/v1/review-issues/{issue_id}

**State transition / edit body / set resolution_note.**

| | |
|---|---|
| Auth | Bearer required |
| Idempotency-Key | Required (UUID) |
| Path params | `issue_id` (uuid) |
| Request body | `ReviewIssuePatchRequest` (см. ниже) |
| Success | `200 OK` → `ReviewIssuePatchResponse` |
| Errors | 400, 401, 404, 409 (base_version mismatch), 422 (invalid transition / missing resolution_note / immutable field change), 500 |

```yaml
ReviewIssuePatchRequest:
  type: object
  required: [base_version]
  properties:
    base_version:
      type: integer
      minimum: 1
      description: Client's last-known version. Must match server's current version.
    state:
      type: string
      enum: [open, ready_for_review, resolved, wont_fix]
      description: Optional. If omitted, state is not changed.
    body:
      type: string
      nullable: true
      maxLength: 4096
      description: Optional. Edit the description.
    resolution_note:
      type: string
      nullable: true
      maxLength: 4096
      description: >
        Optional. Required when transitioning to wont_fix.
        If state transitions away from wont_fix, server auto-clears to null
        (client may omit, server ignores provided value).

ReviewIssuePatchResponse:
  type: object
  required: [issue, project_revision]
  properties:
    issue: { $ref: "#/components/schemas/ReviewIssueBody" }
    project_revision: { type: integer, minimum: 1 }
```

**Server behavior:**
- Проверяет `base_version == current version`. Если нет → `409` с `error.code = "version_conflict"`, response body содержит current `ReviewIssueBody`.
- Если `state` предоставлен и отличается от текущего → применяет transition matrix (§3.1).
- Если `state=wont_fix` → требует non-empty `resolution_note` (в request или уже установленное). `422` если нет.
- Если `state` меняется с `wont_fix` на `open` (Reopen) → server auto-clears `resolution_note` to null.
- Если `state` меняется на `resolved` (первый раз) → server sets `resolved_at`, `resolved_by_device_id`.
- Инкрементирует `version`, `updated_at`.
- Инкрементирует `project_revision`, пишет `ProjectChangeBody` (`operation="update"`).

### 6.4. DELETE /api/v1/review-issues/{issue_id}

**Soft-delete ReviewIssue (atomic version bump).**

| | |
|---|---|
| Auth | Bearer required |
| Idempotency-Key | Required (UUID) |
| Path params | `issue_id` (uuid) |
| Request body | `ReviewIssueDeleteRequest` (см. ниже) |
| Success | `200 OK` → `ReviewIssueDeleteResponse` |
| Errors | 400, 401, 404, 409 (base_version mismatch), 422 (already deleted), 500 |

```yaml
ReviewIssueDeleteRequest:
  type: object
  required: [base_version]
  properties:
    base_version: { type: integer, minimum: 1 }

ReviewIssueDeleteResponse:
  type: object
  required: [issue, project_revision]
  properties:
    issue: { $ref: "#/components/schemas/ReviewIssueBody" }  # with deleted_at set, version incremented
    project_revision: { type: integer, minimum: 1 }
```

**Server behavior (atomic transaction):**
- Проверяет `base_version == current version`. Если нет → `409` с `error.code = "version_conflict"`.
- Atomically выполняет в одной транзакции:
  - `deleted_at = NOW()` (server time).
  - `version = version + 1` (increment, как при PATCH).
  - `updated_at = NOW()`.
  - `project_revision = project_revision + 1`.
- Comments on this issue are NOT deleted (they remain linked to the soft-deleted issue).
- Пишет `ProjectChangeBody` (`entity_type="review_issue"`, `operation="delete"`, `entity_id=issue.id`).
- Если `deleted_at` уже set → `422` с `error.code = "already_deleted"`.
- Response содержит обновлённую `ReviewIssueBody` (с новым `version`, `deleted_at`, `updated_at`).

**Обоснование atomic version bump:** Conflict scenario (§12.5) предполагает, что DELETE с `base_version=5` создаёт tombstone `version=6`. Если DELETE не инкрементирует `version`, последующий PATCH от другого клиента с `base_version=5` пройдёт (некорректно). Atomic `version++` + `deleted_at=NOW()` гарантирует, что любой последующий PATCH увидит `version=6` и получит `409`, а проверка `deleted_at IS NOT NULL` даст `422 issue_deleted`.

### 6.5. POST /api/v1/review-issues/{issue_id}/comments

**Append comment (immutable).**

| | |
|---|---|
| Auth | Bearer required |
| Idempotency-Key | Required (UUID) |
| Path params | `issue_id` (uuid) |
| Request body | `LineCommentCreateRequest` |
| Success | `201 Created` → `LineCommentCreateResponse` |
| Errors | 400, 401, 404 (issue not found), 413 (body too long), 422 (empty body / issue is soft-deleted), 500 |

```yaml
LineCommentCreateRequest:
  type: object
  required: [body]
  properties:
    body: { type: string, minLength: 1, maxLength: 4096 }

LineCommentCreateResponse:
  type: object
  required: [comment, project_revision]
  properties:
    comment: { $ref: "#/components/schemas/LineCommentBody" }
    project_revision: { type: integer, minimum: 1 }
```

**Server behavior:**
- Если issue soft-deleted (`deleted_at != null`) → `422` с `error.code = "issue_deleted"`.
- Comments immutable — server stores as-is, no `edited_at`, no `deleted_at`.
- Инкрементирует `project_revision`, пишет `ProjectChangeBody` (`entity_type="review_comment"`, `operation="create"`).

### 6.6. GET /api/v1/review-issues/{issue_id}/comments

**List comments for an issue.**

| | |
|---|---|
| Auth | Bearer required |
| Idempotency-Key | Not required (GET) |
| Path params | `issue_id` (uuid) |
| Query params | None (no pagination in v0.3) |
| Success | `200 OK` → `LineCommentListResponse` |
| Errors | 400, 401, 404, 500 |

```yaml
LineCommentListResponse:
  type: object
  required: [issue_id, comments]
  properties:
    issue_id: { type: string, format: uuid }
    comments:
      type: array
      items: { $ref: "#/components/schemas/LineCommentBody" }
```

**Behavior:** Возвращает comments в порядке `created_at` ascending. Включает comments даже если parent issue soft-deleted (audit). Сами comments не могут быть удалены (immutable).

### 6.7. POST /api/v1/episodes/{episode_id}/review-transition

**Episode review_state transition.**

| | |
|---|---|
| Auth | Bearer required |
| Idempotency-Key | Required (UUID) |
| Path params | `episode_id` (uuid) |
| Request body | `EpisodeReviewTransitionRequest` |
| Success | `200 OK` → `EpisodeReviewTransitionResponse` |
| Errors | 400, 401, 404, 409 (base_review_state_version mismatch / open_issues_exist), 422 (invalid_transition), 500 |

```yaml
EpisodeReviewTransitionRequest:
  type: object
  required: [target_state, base_review_state_version]
  properties:
    target_state:
      type: string
      enum: [translating, in_review, done]
    base_review_state_version:
      type: integer
      minimum: 1
      description: Client's last-known review_state_version. Must match server's current.

EpisodeReviewTransitionResponse:
  type: object
  required: [episode, project_revision]
  properties:
    episode: { $ref: "#/components/schemas/EpisodeBody" }  # with updated review_state
    project_revision: { type: integer, minimum: 1 }
```

**Server behavior:**
- Проверяет `base_review_state_version == current review_state_version`. Если нет → `409` с `error.code = "version_conflict"`.
- Применяет transition matrix (§4.1).
- Если `in_review → done` → проверяет отсутствие `open`/`ready_for_review` ReviewIssues. Если есть → `409` с `error.code = "open_issues_exist"`, response body содержит count и list of blocking issues.
- Если transition forbidden (e.g. `translating → done`) → `422` с `error.code = "invalid_transition"`.
- Инкрементирует `review_state_version`, `review_state_updated_at`, `review_state_updated_by_device_id`.
- Инкрементирует `project_revision`, пишет `ProjectChangeBody` (`entity_type="episode_review_state"`, `operation="transition"`).

---

## 7. Immutable comments contract

**V0.3 comments are append-only. Это НЕ подлежит изменению в рамках v0.3.**

| Операция | Доступна? |
|---|---|
| `POST /review-issues/{id}/comments` (create) | ✓ |
| `GET /review-issues/{id}/comments` (list) | ✓ |
| `PATCH /comments/{id}` (edit) | ✗ НЕ реализовывать |
| `DELETE /comments/{id}` (delete) | ✗ НЕ реализовывать |

**LineCommentBody schema (финальная):**
- `id`, `issue_id`, `body`, `created_by_device_id`, `created_at`.
- НЕТ `edited_at`.
- НЕТ `deleted_at`.
- `body` имеет `minLength: 1` (server rejects empty with 422).

**Rationale:**
- Сохраняет полный audit trail (ничего не «прячется»).
- Упрощает sync (нет конфликтов редактирования).
- Снижает количество эндпоинтов (2 вместо 4 на comments).

Если команде потребуется редактирование/удаление комментариев в V0.4 — это будет отдельное additive расширение (`PATCH /comments/{id}` + `edited_at`/`deleted_at` поля). Server agent НЕ должен preemptively реализовывать это в v0.3.

---

## 8. (merged into §2 and §9)

---

## 9. Baseline Fingerprint Spec

Этот раздел — interoperable contract. Server и client tests ДОЛЖНЫ использовать одни test vectors.

### 9.1. baseline_text_hash — canonical algorithm

```
INPUT: AssDialogue.Text (полный ASS-текст строки, включая override-теги)

ALGORITHM:
1. Удалить все override-блоки {\...} — оставить только видимый текст.
   Override-блок = последовательность от '{' до '}' включительно.
   Вложенные фигурные скобки внутри override-блока НЕ допускаются (ASS spec),
   но если встречаются — парсер считает блок закрытым на первой '}'.
2. Сохранить \N как literal two-byte sequence (0x5C 0x4E).
   НЕ конвертировать в newline (0x0A).
3. НЕ применять Unicode-нормализацию (NFKC, NFC, NFD — никакую).
   Сравнение exact byte-for-byte.
4. НЕ тримить whitespace (trailing/leading пробелы сохраняются).
5. Закодировать как UTF-8 (Aegisub хранит как UTF-8, но если клиент
   использует другое внутреннее представление — обязан конвертировать в UTF-8
   перед хешированием).
6. SHA-256 от полученных байт.
7. Hex-строка lowercase, 64 символа.

OUTPUT: 64-char lowercase hex string.
```

### 9.2. baseline_timing_hash — canonical algorithm

```
INPUT: line.Start, line.End (в любом внутреннем представлении Aegisub)

ALGORITHM:
1. Канонизировать в centiseconds (integer, 1 cs = 10 ms):
     canonical_start_cs = to_centiseconds(line.Start)
     canonical_end_cs   = to_centiseconds(line.End)
   где to_centiseconds приводит ЛЮБОЕ внутреннее представление
   (int centiseconds, agi::Time, миллисекунды, и т.д.) к целому числу
   centiseconds. Это сознательная граница хеширования: даже если внутренний
   тип изменится или второй клиент использует другое представление,
   hash остаётся стабильным.
2. Каноническая строка: "<start_decimal>|<end_decimal>"
   где start_decimal и end_decimal — десятичные строки:
     - без ведущих нулей (только для start/end > 0; "0" для значения 0)
     - без знака (значения всегда >= 0)
     - без суффикса ("cs", "ms", etc. — НЕ добавлять)
     - пример: "1020|1140" для Start=1020cs, End=1140cs
3. Закодировать как ASCII (каждый символ — 1 байт).
4. SHA-256 от полученных байт.
5. Hex-строка lowercase, 64 символа.

OUTPUT: 64-char lowercase hex string.
```

### 9.3. Test vectors — baseline_text_hash

Server и client tests ДОЛЖНЫ проверять эти вектора (захардкожены в контракт, вычислены по алгоритму §9.1):

| # | Input (visible text, UTF-8) | Expected SHA-256 (lowercase hex) |
|---|---|---|
| T1 | `Hello, world!` | `315f5bdb76d078c43b8ac0064e4a0164612b1fce77c869345bfc94c75894edd3` |
| T2 | `Я защищу их.` | `f900b9efa2b147ec7a5f69cc0f1a42227baa7b5a9b0a7bbdaf32f0381a6e7e66` |
| T3 | `Line with \N break` (literal `\N`, 2 bytes 0x5C 0x4E) | `337b95ccef992ab78628c53ec6f89e712ac18dfd08f565a0d3a6c62c990dff18` |
| T4 | `  trailing spaces  ` (with leading and trailing spaces) | `8feeca5ac770cfe9428ecd09393be5fcb958817b03fd3743c02058c269b7cd63` |
| T5 | `` (empty string — edge case, after override stripping) | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |

**Важно для T3:** input — это literal 19-byte sequence `Line with \N break` где `\N` — два отдельных ASCII байта (0x5C backslash, 0x4E uppercase N), НЕ newline (0x0A). Если реализация конвертирует `\N` в newline перед хешированием — это баг.

**Важно для T4:** input включает leading и trailing spaces (по 2 пробела с каждой стороны). Если реализация тримит whitespace — это баг.

### 9.4. Test vectors — baseline_timing_hash

Server и client tests ДОЛЖНЫ проверять эти вектора (захардкожены в контракт, вычислены по алгоритму §9.2):

| # | Input (start_cs, end_cs) | Canonical string | Expected SHA-256 |
|---|---|---|---|
| U1 | 1020, 1140 | `1020\|1140` | `0f3d7cb8004a09e487bebc54fe81529a4331ef5991a0f621b174f8d604195ddd` |
| U2 | 0, 100 | `0\|100` | `0de72c9841417b2e5acd9bf2f7db6aab7c3cb8bd20ca1a853aa76e1e06f67056` |
| U3 | 1, 2 | `1\|2` | `22074227d8462b39403011e0bc4c5e7a3f1ee1bae54ae2deb0943dece537f93f` |
| U4 | 99999, 100000 | `99999\|100000` | `21fb136eb0a868a3fefa1bc89ea2f7ade8f80bd766a9c47de943899c81d58334` |

**Примечание:** `|` в canonical string — это literal pipe character (ASCII 0x7C), 1 байт. `1020|1140` — 9 ASCII bytes.

### 9.5. Server validation responsibilities

Сервер НЕ вычисляет хеши. Сервер:
- Принимает `baseline_text_hash` / `baseline_timing_hash` от клиента при `POST /review-issues`.
- Валидирует format: `^[0-9a-f]{64}$` (lowercase hex, 64 chars). `422` если невалиден.
- Хранит как opaque string.
- Возвращает в responses и sync без изменений.
- Запрещает изменение в PATCH (`422` если клиент пытается изменить).

Сервер НЕ должен интерпретировать содержимое хешей. Это чисто client-side contract для `modified_after_issue` вычисления.

---

## 10. line_ref contract gate

### 10.1. Status: PENDING (design spike required)

Формат `line_ref` **не определён** в v0.3. Design spike (Phase 0.10 в клиентском плане) должен перебрать варианты и утвердить contract.

### 10.2. Server behavior in v0.3

- `line_ref` — opaque string (`minLength: 1`, `maxLength: 256`).
- Сервер НЕ парсит, НЕ валидирует внутреннюю структуру, НЕ вычисляет.
- Сервер хранит как есть и возвращает в responses/sync.
- Сервер отклоняет пустые строки (`422`).
- Сервер позволяет несколько issues с одинаковым `line_ref` на одном эпизоде (разные QC могут заметить разные проблемы на одной строке).

### 10.3. Client-side interim identity (Phase 3, local-only)

Локальный клиент (до server persistence) может использовать interim identity:
```
interim_line_ref = sha256(normalized_en_text + "|" + start_cs + "|" + end_cs)[:16]
```
Этот interim identity **НЕ покидает устройство** в Phase 3. Он живёт только в локальном сайдкаре.

### 10.4. Server persistence gate (Phase 6)

**Server persistence of ReviewIssue (multi-device sync) активируется ТОЛЬКО после утверждения `line_ref` contract** по результатам spike. До этого:
- `Sanae/ServerReviewSync` feature flag = off.
- ReviewIssues хранятся только локально (degraded mode).
- Серверные эндпоинты могут существовать (для тестирования), но production clients их не используют.

### 10.5. Post-spike contract

После spike, `line_ref` format становится отдельным versioned contract (`line_ref_v1`). Если format изменится в будущем — `line_ref_v2`, и сервер хранит обе. Сервер НЕ конвертирует между версиями.

---

## 11. Sync semantics

### 11.1. What /sync returns (additive to v0.2)

`POST /api/v1/projects/{project_id}/sync` (v0.2) получает два новых массива в response:

```yaml
ProjectSyncResponse:
  ...  # v0.2 fields (from_revision, to_revision, full_snapshot, project, changes,
       # episodes, files, finalized_revisions, terminology, terminology_history, ignored_candidates)
  review_issues:      # NEW in v0.3
    type: array
    items: { $ref: "#/components/schemas/ReviewIssueBody" }
  review_comments:    # NEW in v0.3
    type: array
    items: { $ref: "#/components/schemas/LineCommentBody" }
```

### 11.2. Full snapshot vs delta

Как и v0.2, controlled by `full_snapshot` boolean:

| `full_snapshot=true` | `full_snapshot=false` |
|---|---|
| `review_issues` = ALL issues for the project (including soft-deleted with `deleted_at` set) | `review_issues` = issues touched since `since_revision` (including soft-deleted tombstones) |
| `review_comments` = ALL comments | `review_comments` = comments touched since `since_revision` |

### 11.3. Tombstone propagation

Soft-deleted ReviewIssues появляются в `/sync` с `deleted_at` set. Клиент MUST обработать:
- Если issue есть локально и `deleted_at` отсутствует → mark as deleted locally.
- Если issue есть локально и `deleted_at` уже set → no-op.
- Если issue отсутствует локально → store as-is (tombstone, для future reference).

Comments НЕ имеют tombstones (immutable, никогда не удаляются).

### 11.4. Ordering

`review_issues` и `review_comments` в response НЕ гарантированно отсортированы. Клиент MUST сортировать локально по `updated_at` (issues) или `created_at` (comments) если нужен порядок.

### 11.5. Conflict resolution

Сервер — single source of truth. Если локальное состояние расходится с серверным (например, локальный PATCH не дошёл), серверное состояние побеждает. Клиент:
1. Получает server state из `/sync`.
2. Для каждого ReviewIssue: если `version` на сервере > локального → replace local.
3. Если локальный PATCH был в flight (с `base_version=N`) и сервер уже на `version=N+1` (другой клиент изменил) → PATCH вернёт `409`, клиент MUST re-fetch и retry или показать конфликт.

### 11.6. Stale client (missed revisions)

Если клиент пропустил несколько revisions (long offline), `/sync` с `since_revision=last_known` вернёт delta. Если delta слишком старая (server purged), сервер вернёт `409 invalid_since_revision` (v0.2 behavior), клиент retry с `since_revision=0` → full snapshot.

### 11.7. Authoritative server state

| Поле | Authoritative source |
|---|---|
| `ReviewIssue.state` | Server (client mirrors) |
| `ReviewIssue.version` | Server (client reads, sends as `base_version`) |
| `ReviewIssue.body` | Server |
| `ReviewIssue.resolution_note` | Server |
| `ReviewIssue.baseline_*_hash` | Server (set at creation, immutable) |
| `ReviewIssue.deleted_at` | Server |
| `LineComment.*` | Server (immutable after creation) |
| `EpisodeBody.review_state` | Server |
| `EpisodeBody.review_state_version` | Server |
| `modified_after_issue` (client flag) | Client (computed from `baseline_*_hash` + current line text) |
| `Diagnostic` | Client (never synced) |

---

## 12. Conflict scenarios

Сервер MUST реализовать следующее поведение для каждого сценария:

### 12.1. Two clients simultaneously PATCH same ReviewIssue

- Client A: `PATCH /review-issues/{id}` with `base_version=5`.
- Client B: `PATCH /review-issues/{id}` with `base_version=5`.
- First request to reach server wins (say A's). Server increments `version` to 6.
- B's request: `base_version=5 != current version=6` → `409 Conflict`, `error.code="version_conflict"`, response body contains current `ReviewIssueBody` (version=6).
- **Client B MUST:** re-fetch (use the `ReviewIssueBody` in 409 response), show user the conflict, retry with `base_version=6` if user confirms.

### 12.2. Two clients simultaneously change episode review_state

- Client A: `POST /review-transition` with `base_review_state_version=3`, `target_state=in_review`.
- Client B: `POST /review-transition` with `base_review_state_version=3`, `target_state=done`.
- First wins (say A). Server increments `review_state_version` to 4.
- B's request: `base_review_state_version=3 != current=4` → `409 Conflict`, `error.code="version_conflict"`.
- **Client B MUST:** re-fetch, retry with `base_review_state_version=4` if transition still valid.

### 12.3. POST retry after network timeout

- Client sends `POST /review-issues` with `Idempotency-Key=K`.
- Network timeout. Client doesn't know if server received.
- Client retries with same `Idempotency-Key=K` and same body.
- **If server received first request and created issue:** server returns `200 OK` (NOT `201`) with the original `ReviewIssueCreateResponse` (same issue, same `project_revision`).
- **If server never received first request:** server processes as new, returns `201 Created`.
- `Idempotency-Key` retention: server MUST retain keys for at least 24 hours. After retention expiry, retry with same key may create duplicate. **OPEN CONTRACT QUESTION:** Exact retention period and behavior after expiry.

### 12.4. Comment POST retry with same Idempotency-Key

- Same as 12.3. Server returns original response on retry with same key + same body.
- **If same key + DIFFERENT body:** `422 Unprocessable Entity`, `error.code="idempotency_conflict"`. Server does NOT create duplicate.

### 12.5. Issue deleted on A, edited on B

- Client A: `DELETE /review-issues/{id}` with `base_version=5`. Server sets `deleted_at`, `version=6`.
- Client B (offline, didn't sync): `PATCH /review-issues/{id}` with `base_version=5`, changes `body`.
- B's request: `base_version=5 != current=6` → `409 Conflict`. Response body contains issue with `deleted_at` set.
- **Client B MUST:** detect `deleted_at` in 409 response, NOT retry the edit, show user "issue was deleted by another device".

### 12.6. Issue ready_for_review, another QC already resolved it

- Issue is `ready_for_review`, `version=5`.
- QC A: `PATCH` with `base_version=5`, `state=resolved`. Server: `version=6`, `state=resolved`.
- QC B (didn't refresh): `PATCH` with `base_version=5`, `state=resolved`.
- B's request: `409 Conflict`. Response shows `state=resolved`.
- **Client B MUST:** acknowledge (no-op, desired state already reached), NOT retry.

### 12.7. Mark Done simultaneously with new Open issue creation

- Episode `review_state=in_review`, `review_state_version=3`.
- No non-deleted open/ready_for_review issues.
- Client A: `POST /review-transition` with `base_review_state_version=3`, `target_state=done`.
- Simultaneously, Client B: `POST /review-issues` creates new issue with `state=open`.
- **Серверная atomicity гарантирует один из двух исходов:**
  - **If A's transition reaches server first (acquires lock):** server checks, no open issues → success, `review_state=done`, `version=4`. Then B's create: server checks `review_state == done` → `409 Conflict`, `error.code="episode_review_done"`. Client B MUST first do `done → in_review`, then create issue.
  - **If B's create reaches server first:** issue created (state=open). Then A's transition: server checks, finds open non-deleted issue → `409 Conflict`, `error.code="open_issues_exist"`. Client A MUST resolve/wont_fix the issue first.
- **Сильный invariant сохранён:** `done` всегда означает отсутствие незакрытых ReviewIssues. Серверная транзакция гарантирует, что одновременные операции не создают состояние `done` с open issue.
- Для реализации: server SHOULD использовать row-level lock на `episodes` строке (или `SERIALIZABLE` isolation) во время transition и create, чтобы гарантировать atomicity.

### 12.8. Sync client with stale project revision

- Client's last `project_revision=100`.
- Server is at `project_revision=150`.
- Client calls `/sync` with `since_revision=100`.
- **If server can serve delta:** returns `full_snapshot=false`, arrays with touched entities since revision 100.
- **If server purged delta (too old):** returns `409 Conflict`, `error.code="invalid_since_revision"`. Client MUST retry with `since_revision=0` → full snapshot.

---

## 13. Deletion semantics

### 13.1. ReviewIssue — soft-delete only

- `DELETE /review-issues/{id}` sets `deleted_at`, does NOT remove the row.
- Soft-deleted issues:
  - Appear in `/sync` with `deleted_at` set (tombstone propagation).
  - Excluded from `GET /review-issues?include_deleted=false` (default).
  - Included in `GET /review-issues?include_deleted=true`.
  - CANNOT be PATCHed (server returns `422` with `error.code="issue_deleted"`).
  - CAN have new comments? **No** — `POST /review-issues/{id}/comments` returns `422` with `error.code="issue_deleted"`.
  - Existing comments remain (NOT cascade-deleted).

### 13.2. LineComment — no deletion

Comments are immutable. No `DELETE` endpoint. No `deleted_at` field. Comments on a soft-deleted issue remain accessible via `GET /review-issues/{id}/comments`.

### 13.3. Hard-delete (admin only, not via API)

Hard-delete of ReviewIssues/comments is NOT exposed via API in v0.3. If DB cleanup is needed — admin operation, outside scope. **OPEN CONTRACT QUESTION:** Retention policy for soft-deleted issues (keep forever vs. purge after N days).

### 13.4. Re-creation on same line_ref

Multiple ReviewIssues can exist with the same `line_ref` on the same episode (different QC, different problems). Soft-deleting one does NOT prevent creating a new one with the same `line_ref`.

---

## 14. Author identity

### 14.1. Device attribution, NOT user identity

V0.3 использует `created_by_device_id` (UUID устройства из `DeviceBody`) для атрибуции. Это **device attribution**, не человеческая identity.

### 14.2. Implications

- Один QC, работающий с ноутбука и десктопа = два разных `device_id`. Его issues/comments будут атрибутированы разным устройствам.
- Сервер v0.3 НЕ предоставляет концепцию «мои замечания пользователя».
- Фильтр «мои» в клиентском UI невозможен (клиентский UX убрал этот фильтр — см. UX-план).
- `GET /review-issues` не имеет `?created_by_device_id=` фильтра в v0.3 (можно добавить в v0.4 если потребуется).

### 14.3. No RBAC

Сервер НЕ знает ролей (Translator/Reviewer). Все устройства равны. Любое устройство может:
- Создавать ReviewIssue.
- PATCH state (включая `wont_fix` при наличии `resolution_note`).
- DELETE ReviewIssue.
- Создавать comments.
- Вызывать review-transition.

Ограничения «только Reviewer может WontFix» — **client UX-policy**, не server-enforced. См. §3.3.

### 14.4. Future user identity

Если v0.4+ добавит user accounts — это будет отдельный spike, additive к device model. V0.3 не закладывает архитектурных решений под это (кроме хранения `created_by_device_id`, которое останется даже с user accounts).

---

## 15. Idempotency contract

### 15.1. Scope of Idempotency-Key

| Endpoint | Idempotency-Key | Scope |
|---|---|---|
| `POST /review-issues` | Required | Per (device, key) pair |
| `PATCH /review-issues/{id}` | Required | Per (device, key) pair |
| `DELETE /review-issues/{id}` | Required | Per (device, key) pair |
| `POST /review-issues/{id}/comments` | Required | Per (device, key) pair |
| `POST /review-transition` | Required | Per (device, key) pair |
| `GET /review-issues` | Not used | — |
| `GET /review-issues/{id}/comments` | Not used | — |

### 15.2. Same key + same body

Server returns the original response (same `issue`/`comment`/`episode`, same `project_revision`). HTTP status on retry: `200 OK` (NOT `201 Created`, even if original was `201`).

### 15.3. Same key + same operation fingerprint

Server compares **operation fingerprint**, not just request body hash:

```
fingerprint = SHA256(
    HTTP_method
    + canonical_resource_path  // includes path params: /review-issues/{id}, /episodes/{id}/review-transition
    + canonical_request_body   // canonicalized JSON (sorted keys, no whitespace)
)
```

- **Same key + same fingerprint** → server returns original response (same status, same body). HTTP status on retry: `200 OK` (NOT `201 Created`, even if original was `201`).
- **Same key + different fingerprint** → `422 Unprocessable Entity`, `error.code="idempotency_conflict"`, `error.details` contains the original fingerprint and the new fingerprint for client comparison. Server does NOT execute the new request.

**Зачем fingerprint, не просто body hash:** Представим, что один UUID по ошибке использовали для `PATCH /review-issues/{issue-A}` и `PATCH /review-issues/{issue-B}` с одинаковым JSON body (e.g., `{"state":"resolved"}`). Если сравнивать только body hash — сервер вернул бы response первого endpoint, что некорректно (different resources). Fingerprint включает method + path + body, поэтому разные resources дают разные fingerprints → `422 idempotency_conflict`.

**Canonicalization:**
- HTTP method: uppercase (`PATCH`, `POST`, `DELETE`).
- Path: как есть из HTTP request line, includes path params (`/api/v1/review-issues/abc-123`).
- Body: canonicalized JSON — sorted object keys, no extra whitespace, UTF-8. Server SHOULD use a deterministic JSON serializer.

### 15.4. Retention

- Server MUST retain `Idempotency-Key` → response mapping for at least **24 hours**.
- After retention expiry, server MAY purge the mapping. Retry with same key after purge may create duplicate.
- **OPEN CONTRACT QUESTION:** Exact retention period (24h vs 7d vs 30d). v0.2 spec doesn't specify; v0.3 SHOULD document explicitly.

### 15.5. Cross-device

`Idempotency-Key` scope is **per device** (from `Authorization` token). Two different devices using the same UUID as key is allowed (no conflict). Each device generates its own UUIDs.

---

## 16. DB migration requirements

### 16.1. Recommended schema (PostgreSQL)

Server agent SHOULD use this as reference. Actual implementation may vary if stack differs, but contract (fields, constraints, indexes) MUST match.

```sql
-- ReviewIssue table
CREATE TABLE review_issues (
    id                  UUID PRIMARY KEY,
    project_id          UUID NOT NULL REFERENCES projects(id),
    episode_id          UUID NOT NULL REFERENCES episodes(id),
    line_ref            VARCHAR(256) NOT NULL,
    issue_type          VARCHAR(32) NOT NULL CHECK (issue_type IN ('translation','terminology','timing','style','formatting','other')),
    severity            VARCHAR(16) NOT NULL CHECK (severity IN ('info','warning','error')),
    state               VARCHAR(32) NOT NULL CHECK (state IN ('open','ready_for_review','resolved','wont_fix')),
    body                TEXT,
    resolution_note     TEXT,
    version             INTEGER NOT NULL DEFAULT 1,
    baseline_text_hash  CHAR(64) NOT NULL,
    baseline_timing_hash CHAR(64) NOT NULL,
    created_by_device_id UUID NOT NULL REFERENCES devices(id),
    created_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    resolved_at         TIMESTAMPTZ,
    resolved_by_device_id UUID REFERENCES devices(id),
    deleted_at          TIMESTAMPTZ,

    CONSTRAINT chk_resolution_note CHECK (
        (state = 'wont_fix' AND resolution_note IS NOT NULL AND resolution_note != '')
        OR (state != 'wont_fix' AND resolution_note IS NULL)
    )
);

CREATE INDEX idx_review_issues_episode_id ON review_issues(episode_id);
CREATE INDEX idx_review_issues_episode_state ON review_issues(episode_id, state) WHERE deleted_at IS NULL;
CREATE INDEX idx_review_issues_project_updated ON review_issues(project_id, updated_at);
CREATE INDEX idx_review_issues_deleted_at ON review_issues(deleted_at) WHERE deleted_at IS NOT NULL;

-- LineComment table (immutable — no updated_at, no deleted_at)
CREATE TABLE review_comments (
    id                  UUID PRIMARY KEY,
    issue_id            UUID NOT NULL REFERENCES review_issues(id),
    body                TEXT NOT NULL CHECK (body != ''),
    created_by_device_id UUID NOT NULL REFERENCES devices(id),
    created_at          TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_review_comments_issue_id ON review_comments(issue_id, created_at);

-- Idempotency key store
CREATE TABLE idempotency_keys (
    device_id           UUID NOT NULL REFERENCES devices(id),
    key                 UUID NOT NULL,
    endpoint            VARCHAR(128) NOT NULL,
    request_body_hash   VARCHAR(64) NOT NULL,
    response_status     INTEGER NOT NULL,
    response_body       TEXT NOT NULL,
    created_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    expires_at          TIMESTAMPTZ NOT NULL,

    PRIMARY KEY (device_id, key)
);

CREATE INDEX idx_idempotency_expires ON idempotency_keys(expires_at);

-- Episode table — additive columns
ALTER TABLE episodes ADD COLUMN review_state VARCHAR(32) NOT NULL DEFAULT 'translating'
    CHECK (review_state IN ('translating','in_review','done'));
ALTER TABLE episodes ADD COLUMN review_state_version INTEGER NOT NULL DEFAULT 1;
ALTER TABLE episodes ADD COLUMN review_state_updated_at TIMESTAMPTZ;
ALTER TABLE episodes ADD COLUMN review_state_updated_by_device_id UUID REFERENCES devices(id);

CREATE INDEX idx_episodes_review_state ON episodes(review_state) WHERE deleted_at IS NULL;
```

### 16.2. Migration / backfill

```sql
-- All existing episodes get review_state='translating', review_state_version=1
-- (default values in ALTER TABLE cover this)
```

### 16.3. Indexes rationale

| Index | Purpose |
|---|---|
| `idx_review_issues_episode_state` | `GET /review-issues?state=open` |
| `idx_review_issues_project_updated` | `/sync` delta by `project_revision` (via changes table) |
| `idx_review_issues_deleted_at` | Tombstone filtering |
| `idx_review_comments_issue_id` | `GET /review-issues/{id}/comments` ordered |
| `idx_idempotency_expires` | Purge expired keys |

### 16.4. Constraints rationale

- `chk_resolution_note` CHECK — enforces invariant из §3.2 на DB уровне (defence in depth, не полагается только на app logic).
- `body != ''` CHECK на comments — enforces `minLength: 1`.
- Foreign keys — `project_id`, `episode_id`, `issue_id`, `created_by_device_id` — для referential integrity.

### 16.5. ProjectChangeBody extension

`project_changes` table (v0.2) получает новые `entity_type` значения. No schema change needed if `entity_type` is `VARCHAR`. Если v0.2 использует enum — расширить enum.

---

## 17. Backward compatibility matrix

| Client \ Server | v0.2 | v0.3 |
|---|---|---|
| **v0.2 client** | Full functionality (v0.2 baseline). | Full v0.2 functionality. v0.3 server returns new fields in `/sync` and `EpisodeBody` — v0.2 client ignores unknown fields (JSON tolerant). v0.2 client never calls new endpoints. |
| **v0.3 client** | **Degraded mode.** ReviewIssues stored locally only (sidecar). `episode.review_state` local only. Multi-device QC sync not available. `/sync` succeeds (v0.2 server omits new arrays). Client detects via capability detection (§18). | Full functionality. Multi-device QC sync active (after `line_ref` spike, §10). |

### 17.1. Degraded mode details (v0.3 client + v0.2 server)

- ReviewIssues: stored in `<file>.ass.aegisub.json` sidecar. Not synced.
- Comments: stored in sidecar. Not synced.
- `episode.review_state`: local only, in sidecar.
- `modified_after_issue`: works (baseline fingerprints computed locally).
- Diagnostics: always local, unaffected.
- QCProfile, aliases: always local-only (even with v0.3 server, see §16 of UX plan).
- `/finalize`: works as v0.2 (no new preconditions).

**Client behavior in degraded mode:** feature flag `Sanae/ServerReviewSync = off`. Client does NOT attempt to call new endpoints (detects via §18). UI shows "offline QC" indicator.

---

## 18. Capability detection

### 18.1. Solution: `server_capabilities` field in `/sync` response (v0.3)

V0.3 сервер ДОБАВЛЯЕТ опциональное поле `server_capabilities` в `ProjectSyncResponse`:

```yaml
ProjectSyncResponse:
  ...  # v0.2 fields + v0.3 review_issues, review_comments
  properties:
    ...
    server_version:
      type: string
      description: >
        Server version string, e.g. "0.3.0". Absent in v0.2 servers.
        Client uses this to detect server capabilities.
      example: "0.3.0"
    server_capabilities:
      type: array
      items: { type: string }
      description: >
        List of capability strings supported by this server. Absent in v0.2 servers.
        v0.3 servers return at least:
        ["review_issues", "review_comments", "episode_review_state", "review_transition"].
      example: ["review_issues", "review_comments", "episode_review_state", "review_transition"]
```

### 18.2. Client detection logic

```
1. Client calls POST /projects/{id}/sync.
2. If response contains server_capabilities array:
   - If "review_issues" in capabilities → enable Sanae/ServerReviewSync.
   - If "review_transition" in capabilities → enable Submit for QC / Mark Done.
3. If response does NOT contain server_capabilities (v0.2 server):
   - Stay in degraded mode (local-only ReviewIssues, review_state in sidecar).
   - Do NOT attempt to call new endpoints.
```

### 18.3. Why not probing

Probing (attempting `GET /review-issues` and interpreting `404` vs `200`) is brittle:
- `404` might mean "endpoint doesn't exist" OR "episode not found" OR "server misconfigured".
- Adds latency on every client startup.
- Doesn't scale to future capabilities.

`server_capabilities` is explicit, cheap (one field in existing `/sync` response), and extensible (new capabilities added as new strings, backward-compatible).

### 18.4. Future capabilities

V0.4+ may add: `"terminology_aliases_sync"`, `"qc_profile_sync"`, `"editable_comments"`, `"user_accounts"`, etc. Client checks for specific capability strings before using corresponding features.

---

## 19. Server tests checklist

Server coding agent MUST implement these tests:

### 19.1. State transition unit tests (ReviewIssue)

- [ ] `open → ready_for_review` succeeds, `version` increments.
- [ ] `open → resolved` succeeds, `resolved_at` set.
- [ ] `open → wont_fix` with non-empty `resolution_note` succeeds.
- [ ] `open → wont_fix` with null/empty `resolution_note` → `422`.
- [ ] `open → wont_fix` with `resolution_note` provided in prior PATCH (state already wont_fix) → no-op or `422` if `resolution_note` cleared.
- [ ] `ready_for_review → open` (Return) succeeds.
- [ ] `ready_for_review → resolved` succeeds.
- [ ] `ready_for_review → wont_fix` requires `resolution_note`.
- [ ] `resolved → open` (Reopen) succeeds. `resolved_at` and `resolved_by_device_id` CLEARED to null (see §3.2). Historical audit preserved in `ProjectChangeBody`.
- [ ] `resolved → ready_for_review` → `422 invalid_transition`.
- [ ] `resolved → wont_fix` → `422 invalid_transition`.
- [ ] `wont_fix → open` (Reopen) succeeds, `resolution_note` auto-cleared to null.
- [ ] `wont_fix → ready_for_review` → `422 invalid_transition`.
- [ ] `wont_fix → resolved` → `422 invalid_transition`.
- [ ] `resolution_note` non-null when `state != wont_fix` → `422`.
- [ ] `baseline_text_hash` change in PATCH → `422`.
- [ ] `baseline_timing_hash` change in PATCH → `422`.

### 19.2. State transition unit tests (episode review_state)

- [ ] `translating → in_review` succeeds, `review_state_version` increments.
- [ ] `in_review → translating` (Return) succeeds.
- [ ] `in_review → done` with no open/ready_for_review issues succeeds.
- [ ] `in_review → done` with open issue → `409 open_issues_exist`.
- [ ] `in_review → done` with ready_for_review issue → `409 open_issues_exist`.
- [ ] `in_review → done` with only resolved/wont_fix issues succeeds.
- [ ] `done → in_review` (Reopen) succeeds.
- [ ] `done → translating` → `422 invalid_transition`.
- [ ] `translating → done` → `422 invalid_transition`.
- [ ] `base_review_state_version` mismatch → `409 version_conflict`.

### 19.3. Optimistic concurrency tests

- [ ] PATCH with correct `base_version` succeeds.
- [ ] PATCH with stale `base_version` → `409`, response contains current `ReviewIssueBody`.
- [ ] review-transition with correct `base_review_state_version` succeeds.
- [ ] review-transition with stale `base_review_state_version` → `409`.

### 19.4. Idempotency tests

- [ ] POST with same `Idempotency-Key` + same body → same response, `200` on retry.
- [ ] POST with same `Idempotency-Key` + different body → `422 idempotency_conflict`.
- [ ] POST with different `Idempotency-Key` + same body → two distinct resources.
- [ ] Idempotency works for PATCH, DELETE, review-transition, comment create.

### 19.5. Comments immutability tests

- [ ] `POST /comments` creates comment with `body` non-empty.
- [ ] `POST /comments` with empty body → `422`.
- [ ] `PATCH /comments/{id}` → `404 Not Found` (endpoint doesn't exist).
- [ ] `DELETE /comments/{id}` → `404 Not Found`.
- [ ] Comment on soft-deleted issue → `422 issue_deleted`.
- [ ] `GET /comments` on soft-deleted issue → `200` with comments (audit).

### 19.6. Sync propagation tests

- [ ] Create ReviewIssue on device A → `/sync` on device B returns it in `review_issues`.
- [ ] PATCH ReviewIssue on A → `/sync` on B returns updated issue.
- [ ] DELETE ReviewIssue on A → `/sync` on B returns issue with `deleted_at` set (tombstone).
- [ ] Create comment on A → `/sync` on B returns it in `review_comments`.
- [ ] review-transition on A → `/sync` on B returns updated `EpisodeBody.review_state`.
- [ ] `full_snapshot=true` returns ALL issues (including deleted).
- [ ] `full_snapshot=false` returns only touched issues since `since_revision`.
- [ ] Stale `since_revision` → `409 invalid_since_revision`, client retries with 0.

### 19.7. Tombstone tests

- [ ] Soft-deleted issue appears in `/sync` with `deleted_at`.
- [ ] Soft-deleted issue excluded from `GET ?include_deleted=false`.
- [ ] Soft-deleted issue included in `GET ?include_deleted=true`.
- [ ] PATCH on soft-deleted issue → `422 issue_deleted`.
- [ ] Comments on soft-deleted issue remain accessible.

### 19.8. Backward compatibility tests

- [ ] v0.2 client calls `/sync` → response has v0.2 fields + new arrays (v0.2 client ignores new).
- [ ] v0.2 client calls `/finalize` → no new preconditions enforced.
- [ ] v0.3 client + v0.2 server → `/sync` succeeds without new arrays, client enters degraded mode.

### 19.9. Invalid line_ref tests

- [ ] `POST /review-issues` with `line_ref=""` (empty) → `422`.
- [ ] `POST /review-issues` with `line_ref` > 256 chars → `422`.
- [ ] `POST /review-issues` with valid `line_ref` → `201`.
- [ ] Server does NOT parse `line_ref` internal structure (opaque).

### 19.10. Baseline hash validation tests

- [ ] `POST /review-issues` with `baseline_text_hash` not matching `^[0-9a-f]{64}$` → `422`.
- [ ] `POST /review-issues` with uppercase hex → `422` (lowercase required).
- [ ] `POST /review-issues` with valid hash → `201`.
- [ ] Server stores hash as opaque string (does not recompute).

### 19.11. Mark Done race condition (§12.7)

- [ ] `review-transition` to `done` and `POST /review-issues` concurrently — if transition first, issue create succeeds (invariant temporarily violated, client detects on sync).
- [ ] If issue create first, transition → `409 open_issues_exist`.

### 19.12. Finalize no-precondition tests

- [ ] `/finalize` with `review_state=translating` → succeeds (no new precondition).
- [ ] `/finalize` with `review_state=in_review` → succeeds.
- [ ] `/finalize` with `review_state=done` → succeeds.
- [ ] `/finalize` with open ReviewIssues → succeeds (server does not check).

---

## 20. Client ↔ Server acceptance matrix

| Requirement | Server behavior | Client behavior | Test | Blocking for Phase 6? |
|---|---|---|---|---|
| Create ReviewIssue | `POST /review-issues` returns `201` with full body | Sends `baseline_*_hash`, `line_ref`, `issue_type`, `severity` | §19.1, §19.9 | Yes |
| ReviewIssue state machine | Enforces transition matrix (§3.1), `resolution_note` for wont_fix | Sends `base_version`, handles `409` | §19.1 | Yes |
| Episode review_state | Enforces transition matrix (§4.1), `open_issues_exist` for `in_review→done` | Sends `base_review_state_version`, handles `409` | §19.2 | Yes |
| Immutable comments | No PATCH/DELETE on comments | Never calls them | §19.5 | Yes |
| Baseline fingerprints | Stores as opaque `^[0-9a-f]{64}$` strings | Computes per §9, sends in create | §19.10 | Yes |
| Sync propagation | Returns `review_issues`, `review_comments` in `/sync` | Merges, handles tombstones | §19.6, §19.7 | Yes |
| Idempotency | Retains keys 24h+, returns same response on retry | Retries with same key on timeout | §19.4 | Yes |
| Optimistic concurrency | `409` on `base_version` mismatch | Re-fetches from `409` body, retries or shows conflict | §19.3 | Yes |
| Soft-delete | Sets `deleted_at`, propagates via sync | Marks local as deleted, excludes from default list | §19.7, §19.13 | Yes |
| Finalize no-precondition | `/finalize` works regardless of `review_state` | Shows optional warning if `review_state != done` | §19.12 | Yes |
| Capability detection | Returns `server_capabilities` array in `/sync` | Reads `server_capabilities` from `/sync`; enables features per capability string | §18, §19.8 | Yes |
| `line_ref` format | Opaque string, no parsing | Uses interim identity locally, final format after spike | §10, §19.9 | Yes (for multi-device) |
| Backward compat | v0.2 fields unchanged, new fields additive | Ignores unknown fields, enters degraded mode on v0.2 server | §19.8 | No |

---

## SERVER AGENT TODO — IMPLEMENTATION ORDER

Разбейте серверную работу на маленькие независимые PR. Каждый PR — отдельный merge с tests.

### S0 — OpenAPI contract only (no implementation)

**Goal:** Зафиксировать контракт в OpenAPI spec до любой реализации.

**Deliverables:**
- Обновлённый `openapi.yaml` с новыми schemas (`ReviewIssueBody`, `LineCommentBody`, `ReviewIssueCreateRequest`, `ReviewIssuePatchRequest`, etc.), новыми endpoints (§6), расширенными `EpisodeBody`, `ProjectSyncResponse`, `ProjectChangeBody`.
- Test vectors для baseline fingerprints (§9.3, §9.4) — computed independently.

**Dependencies:** None.
**Files:** `openapi.yaml` (or equivalent).
**Tests:** Spec validation (e.g. `openapi-generator-cli validate`).
**Definition of Done:** Spec reviewed by client team, all OPEN CONTRACT QUESTIONS resolved or explicitly deferred.

---

### S1 — DB migration

**Goal:** Schema changes, no app logic.

**Deliverables:**
- Migration script (§16): `review_issues`, `review_comments`, `idempotency_keys` tables; `episodes` additive columns.
- Constraints: `chk_resolution_note`, `body != ''`.
- Indexes (§16.3).
- Backfill: existing episodes get `review_state='translating'`, `review_state_version=1`.

**Dependencies:** S0 (contract must be settled).
**Files:** DB migration files (e.g. `migrations/001_add_review_issues.sql`).
**Tests:** Migration runs on empty DB + on DB with existing v0.2 data. Rollback tested.
**Definition of Done:** Migration applies cleanly, constraints enforced, indexes created.

---

### S2 — ReviewIssue CRUD

**Goal:** Endpoints 6.1, 6.2, 6.3, 6.4 (create, list, patch, delete). No comments, no review-transition yet.

**Deliverables:**
- `POST /episodes/{id}/review-issues` (create).
- `GET /episodes/{id}/review-issues` (list with filters).
- `PATCH /review-issues/{id}` (state transition, body edit, resolution_note).
- `DELETE /review-issues/{id}` (soft-delete).
- Transition matrix enforcement (§3.1, §3.2).
- Optimistic concurrency (`base_version`).
- `ProjectChangeBody` audit entries.

**Dependencies:** S1.
**Files:** ReviewIssue controller, service, repository.
**Tests:** §19.1, §19.3, §19.9, §19.10, §19.12 (finalize no-precondition — verify `/finalize` still works).
**Definition of Done:** All state transition tests pass, optimistic concurrency works, audit entries written.

---

### S3 — Immutable comments

**Goal:** Endpoints 6.5, 6.6 (comment create, list). No edit/delete.

**Deliverables:**
- `POST /review-issues/{id}/comments` (create, immutable).
- `GET /review-issues/{id}/comments` (list).
- `body` minLength=1 validation.
- Reject comment on soft-deleted issue.
- `ProjectChangeBody` audit entries.

**Dependencies:** S2.
**Files:** LineComment controller, service, repository.
**Tests:** §19.5.
**Definition of Done:** Comments created/listed, immutability enforced (no PATCH/DELETE endpoints exist), empty body rejected.

---

### S4 — Episode review state machine

**Goal:** Endpoint 6.7 (review-transition). State machine with invariants.

**Deliverables:**
- `POST /episodes/{id}/review-transition`.
- Transition matrix (§4.1) enforcement.
- `in_review → done` precondition: no open/ready_for_review issues (§4.2).
- `review_state_version` optimistic concurrency.
- `EpisodeBody` additive fields in all episode responses.
- `ProjectChangeBody` audit entries (`entity_type="episode_review_state"`).

**Dependencies:** S2 (ReviewIssue must exist for `in_review → done` check).
**Files:** Episode controller (extended), review-transition service.
**Tests:** §19.2, §19.11 (race condition).
**Definition of Done:** All transitions enforced, `open_issues_exist` check works, version concurrency works.

---

### S5 — Sync integration

**Goal:** Extend `ProjectSyncResponse` with `review_issues` and `review_comments` arrays.

**Deliverables:**
- `/sync` response includes `review_issues`, `review_comments`.
- Full snapshot vs delta logic (§11.2).
- Tombstone propagation (soft-deleted issues with `deleted_at`).
- `ProjectChangeBody` entries for all new entity types.
- Stale `since_revision` handling (v0.2 behavior, unchanged).

**Dependencies:** S2, S3, S4.
**Files:** Sync service (extended).
**Tests:** §19.6, §19.7.
**Definition of Done:** Multi-device sync works, tombstones propagate, delta vs full snapshot correct.

---

### S6 — Idempotency & concurrency hardening

**Goal:** Robust idempotency and concurrency under edge cases.

**Deliverables:**
- `Idempotency-Key` enforcement on all mutations (§15).
- Same key + same operation fingerprint (method + path + body) → same response.
- Same key + different fingerprint → `422 idempotency_conflict`.
- Retention (24h minimum, configurable).
- Conflict scenario tests (§12.1–12.8).

**Dependencies:** S2, S3, S4, S5.
**Files:** Idempotency middleware, key store.
**Tests:** §19.4, §12.1–12.8 (all conflict scenarios).
**Definition of Done:** All idempotency tests pass, all conflict scenarios produce correct HTTP status + client-recoverable response.

---

### S7 — Compatibility & integration tests

**Goal:** Verify backward compatibility and end-to-end integration.

**Deliverables:**
- v0.2 client + v0.3 server compatibility test.
- v0.3 client + v0.2 server degraded mode test.
- Capability detection (§18) — `server_capabilities` field in `/sync` response (v0.3 server MUST implement; not probing).
- Full `/sync` → local merge → PATCH → `/sync` round-trip.

**Dependencies:** S0–S6.
**Files:** Integration test suite, capability detection.
**Tests:** §19.8, §18.
**Definition of Done:** All 4 compatibility cells (§17) verified, capability detection works.

---

### S8 — Client-server end-to-end acceptance

**Goal:** Joint testing with client team. Verify acceptance matrix (§20).

**Deliverables:**
- End-to-end test scenarios with real client.
- Acceptance matrix (§20) — every row verified.
- Performance baseline (sync with 1000+ issues, < 1s).

**Dependencies:** S7, client Phase 3 (local ReviewIssue) + Phase 6 (server sync).
**Files:** E2E test report.
**Tests:** §20 (all rows).
**Definition of Done:** All acceptance matrix rows pass, client and server teams sign off.

---

## RESOLVED CONTRACT QUESTIONS (summary)

Все вопросы из предыдущих ревизий разрешены в v0.3-final:

| # | Question | Resolution |
|---|---|---|
| 1 | `resolved_at` / `resolved_by_device_id` on Reopen: clear or preserve? | **CLEARED on Reopen** (§3.2). `resolved_at` означает состояние текущего закрытия. Исторический audit — в `ProjectChangeBody`. |
| 2 | Idempotency-Key retention period | **24 hours minimum** (§15.4). |
| 3 | Soft-deleted issue retention policy | **Keep forever** (v0.3) (§13.3). |
| 4 | Reject issue creation when `review_state=done`? | **YES, reject** with `409 episode_review_done` (§4.2, §6.1). Сильный invariant: `done` = нет незакрытых issues. |
| 5 | Capability detection: probing vs. `server_capabilities` | **`server_capabilities` field in `/sync`** (§18.1). v0.3 server MUST implement. Not probing. |
| 6 | Internal Aegisub time representation | Client confirms in Phase 0.12; canonical `to_centiseconds()` conversion regardless (§9.2). |
| 7 | Idempotency fingerprint: body hash vs. operation fingerprint | **Operation fingerprint** = SHA256(method + path + canonical body) (§15.3). |
| 8 | Baseline fingerprint test vectors | **Hardcoded** in contract (§9.3, §9.4). Computed by independent review. |
| 9 | `Mark Done` with soft-deleted open issues | Soft-deleted issues **ignored** by `in_review → done` check (§4.2: `state IN (...) AND deleted_at IS NULL`). |

Server agent SHOULD implement per resolutions above. No defaults to fall back on.

---

**End of Sanae Server Requirements v0.3 (final, frozen).**
