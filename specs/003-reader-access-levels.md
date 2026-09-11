# 003 — Reader Access Levels & Chapter Assignment

## Summary
Extends the `access.txt` registry (managed by the shared `AccessRegistry` class, spec 002)
so the writer can assign each reader an access level — Master, Apprentice, or Novice —
controlling which encrypted chapters that reader's key gets wrapped for.

## Motivation / Problem
Spec 002's `ChapterEncryptionService` wraps the AES content-key for every authorized
reader, for every chapter. The writer wants finer control: some readers should see
everything (Master), some only specific chapters the writer picks (Apprentice), and some
only a single chapter (Novice) — e.g. giving someone a "preview" of just one chapter
without exposing the rest of the story.

## Architecture Context
This spec's behavior is owned by **`ReaderAccessController`**
(`src/writer/ReaderAccessController.{h,cpp}`), a class that operates over the shared
**`AccessRegistry`** (`src/shared/AccessRegistry.{h,cpp}`, spec 002) rather than parsing
`access.txt` itself. It's reached from `WriterMenu`'s "Manage reader access" item, and is
also called into by `ChapterEncryptionService` (spec 002 step 2 and step 6) during
registration and per-chapter wrap decisions — there is exactly one place in the codebase
that knows the level/chapter-list rules.

## User Stories
- As a writer, I want to mark a trusted reader as Master so they always get access to
  every chapter, past and future, without re-listing chapters each time.
- As a writer, I want to give a reader access to only specific chapters (e.g. 2 and 3,
  not 1 or 4) by assigning them Apprentice with that exact chapter list.
- As a writer, I want to give someone a single-chapter preview (Novice) without
  exposing anything else.
- As a writer, I want to change a reader's level or chapter list later (promote,
  narrow, or expand their access) without having to remove and re-add them.

## Scope
**In scope:**
- Extending `AccessRegistry`'s line format to carry level + chapter-list alongside the
  existing name + public key (the 4-field format `AccessRegistry` always uses — see spec
  002's Data Model, which already reflects this).
- `ReaderAccessController::collectRegistrationDetails()` — called from
  `ChapterEncryptionService`'s reader-registration step (spec 002 step 2) to require an
  explicit level choice (and chapter list, if applicable) — no registration without one.
- `ReaderAccessController::editExistingReader(name)` — a new `WriterMenu` entry point:
  re-run assignment against an existing name to change their level/chapter-list (name and
  public key untouched, via `AccessRegistry::updateReader`).
- `ReaderAccessController::isAuthorizedForChapter(ReaderEntry, chapterNumber)` — called by
  `ChapterEncryptionService::encryptChapter` (spec 002 step 6) per reader per chapter, to
  decide whether that reader's copy of the AES key gets wrapped for this chapter.
- Validation: Apprentice requires a non-empty chapter list; Novice requires exactly
  one chapter; Master requires none (implicitly all chapters, present and future).

**Out of scope (future features):**
- Removing/revoking a reader entirely from `access.txt` (this feature only changes
  level/chapter-list, not deletion) — flagged as a related but separate future need.
- Reader-side self-service chapter requests.
- Rotating a reader's public key during an edit (edit only touches level/chapters).
- Retroactively re-wrapping already-encrypted chapters when a reader's access changes
  (inherits spec 002's existing precedent: changes only affect future encryption
  runs unless the writer deliberately re-encrypts an old chapter).

## Behavior / Functional Requirements
1. **Extended `access.txt` line format** (always exactly 4 colon-delimited fields,
   enforced by `AccessRegistry`): `<name>:<base64-public-key>:<level>:<chapters>`
   - `<level>` is exactly one of `Master`, `Apprentice`, `Novice`.
   - `<chapters>` is a comma-separated list of chapter numbers (e.g. `2,3`) for
     Apprentice — stored internally as a `std::unordered_set<int>` for O(1) membership
     checks and free duplicate-number detection on insert — exactly one number (e.g. `1`)
     for Novice, and empty for Master (e.g. `alice:MIIB...:Master:`).
2. **Registration (called from spec 002 step 2):** after `ChapterEncryptionService`
   collects name + public key, `ReaderAccessController::collectRegistrationDetails()`
   always prompts for a level. If Apprentice: prompt for one or more chapter numbers
   (must be non-empty). If Novice: prompt for exactly one chapter number. If Master:
   no further prompt. Returns the validated level/chapter-set back to
   `ChapterEncryptionService`, which passes it to `AccessRegistry::appendReader` to
   write the 4-field line and commit (e.g. `chore: add reader <name> (<level>)`).
3. **Edit existing reader (`WriterMenu` item):** writer selects an existing name (via
   `AccessRegistry::findByName`, O(1) lookup), is prompted for a new level (and chapter
   list, if applicable) using the same validation as registration, and
   `AccessRegistry::updateReader` rewrites the matching line in place (name and public
   key unchanged). Commit as its own change (e.g. `chore: update access for <name> to
   <level>`).
4. **Per-chapter wrapping decision (`isAuthorizedForChapter`, called from spec 002 step
   6):** when encrypting chapter number `N`, for each reader in `AccessRegistry`, this
   returns true only if: level is Master, OR level is Apprentice and `N` is in their
   chapter set, OR level is Novice and `N` equals their single chapter. Otherwise false
   (no wrapped-key entry for that reader on that chapter).
5. Chapter numbers may reference chapters that don't exist yet (the writer can
   pre-authorize a future chapter number) — this is allowed, not an error.

## Data Model / API / CLI Surface
- **`access.txt`** (repo root, git-tracked, plaintext, no secrets) — format owned by
  `AccessRegistry`, always 4 fields:
  ```
  alice:MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA...:Master:
  bob:MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA...:Apprentice:2,3
  carol:MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA...:Novice:1
  ```
- **Class:** `ReaderAccessController` (`src/writer/ReaderAccessController.{h,cpp}`),
  invoked from `WriterMenu`'s "Manage reader access" item and called into by
  `ChapterEncryptionService`.
- No new sensitive data introduced (level/chapter numbers aren't secret) — no new
  `security-reviewer` concerns beyond what spec 002 already flagged for the public key
  field itself.

## Edge Cases & Error Handling
- Unknown/misspelled level string → reject at input time (registration/edit), don't
  write an invalid line to `access.txt`.
- Apprentice with an empty chapter list → reject, re-prompt (must name at least one).
- Novice with more than one chapter number → reject, re-prompt (exactly one).
- Non-numeric or duplicate chapter numbers in a list → reject with a clear message (the
  `std::unordered_set<int>` backing makes duplicate detection a natural side effect of
  insertion rather than a separate scan).
- Existing malformed line found in `access.txt` at encryption time (e.g. hand-edited
  incorrectly) → `AccessRegistry` skips that reader for that run with a warning naming
  the line, same pattern as spec 002's malformed-line handling — don't abort the whole
  run.
- Editing a name that doesn't exist in `access.txt` → clear error, no line created
  (edit is not an implicit registration).
- Chapter list referencing a chapter number that doesn't exist yet → allowed (see
  Behavior item 5), not an error.

## Acceptance Criteria
1. Registering a new reader always requires an explicit level choice; Apprentice
   requires ≥ 1 chapter, Novice requires exactly 1 — no registration proceeds
   otherwise.
2. After registration, `access.txt` contains a well-formed 4-field line matching the
   chosen name/key/level/chapters.
3. Editing an existing reader changes only their level/chapter-list line; name and
   public key are unchanged; produces its own commit.
4. Encrypting chapter number `N` produces a `.enc` file whose `wrappedKeys` include
   exactly: all Master readers, Apprentice readers with `N` in their set, and Novice
   readers whose chapter equals `N` — no others.
5. Malformed level/chapter input at registration or edit time is rejected with a
   re-prompt, never silently written to `access.txt`.
6. Changing a reader's access after chapter `N` was already encrypted does not alter
   that existing `.enc` file — only chapters encrypted after the change reflect it.

## Open Questions
- Full removal/revocation of a reader from `access.txt` (not just narrowing their
  chapter list) is a related, natural need this feature doesn't cover — flagged for a
  possible future feature rather than assumed in scope here.
- Public-key rotation during an edit (e.g. a reader lost their private key and
  generates a new pair) isn't covered — currently would require removing and
  re-adding the reader as a new entry.
