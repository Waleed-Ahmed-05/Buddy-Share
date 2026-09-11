# 002 — Chapter Content Encryption (Hybrid AES + RSA, Multi-Reader)

## Summary
Encrypts chapter content with a password-derived AES key before it's committed/pushed
to git, and separately wraps that AES key with each authorized reader's RSA public key
(from a git-tracked `access.txt` registry) — so multiple distinct readers can each
independently decrypt using only their own private key, without ever sharing the
writer's password.

## Motivation / Problem
Chapters get pushed to a GitHub repo per feature 001, which today means plaintext prose
in git history, visible to anyone with repo access. The writer wants confidentiality:
content becomes unintelligible ciphertext in git, and only specific people they've
explicitly granted access to (by adding that person's public key) can turn it back into
readable chapters — with the ability to add or remove a reader later without changing
a shared password for everyone else.

## Architecture Context
This spec's behavior lives inside `BuddyShare.exe` (see spec 001) as a `WriterMenu` item
("Encrypt a chapter"), reachable only after the spec 001 bootstrap has completed. It owns:
- **`ChapterEncryptionService`** (`src/writer/ChapterEncryptionService.{h,cpp}`) — the
  encrypt/wrap/commit/push behavior below.
- Uses, but does not own, the shared classes defined once and reused across writer and
  reader code:
  - **`AccessRegistry`** (`src/shared/AccessRegistry.{h,cpp}`) — the single parser/writer
    for `access.txt`. Backed by `std::unordered_map<std::string, ReaderEntry>` keyed by
    reader name for O(1) lookup (used here for duplicate-name checks during registration,
    and reused unchanged by spec 004's reader-side own-entry lookup).
  - **`ChapterFile`** (`src/shared/ChapterFile.{h,cpp}`) — the one definition of the `.enc`
    JSON envelope, read by the reader (spec 004) and written here.
  - **`CryptoProvider`** (`src/shared/CryptoProvider.{h,cpp}`) — PBKDF2, AES-256-GCM,
    RSA-OAEP, backed by OpenSSL. The same class instance's parameters are used for both
    this spec's encryption and spec 004's decryption, which eliminates the cross-language
    parameter-mismatch risk the original C++/PowerShell split would have had.
- Reader registration's level/chapter-list requirement (step 2 below) is defined in full by
  spec 003's `ReaderAccessController`; this spec calls into it rather than re-describing it.

## User Stories
- As a writer, I want to type one password and have my chapter turn into unreadable
  ciphertext before it's committed, so plaintext never touches git history.
- As a writer, I want to grant a specific person read access by adding their public
  key to a registry, without needing to share a password with them out of band.
- As a writer, I want to add a new reader later without re-encrypting every past
  chapter for everyone else.
- As a reader, I want to use my own private key (generated myself, never shared) to
  unlock chapters encrypted for me — I never need to know the writer's password.

## Scope
**In scope:**
- AES-256-GCM encryption of chapter content, keyed via a KDF from a writer-typed
  password (never the raw password as the key), via `CryptoProvider`.
- A git-tracked registry file, `access.txt`, at the repo root: reader identity + RSA
  public key, one per line, parsed/written by `AccessRegistry`.
- Per chapter, the AES content-key is RSA-OAEP-wrapped once per reader currently listed
  in `access.txt` and authorized for that chapter (per spec 003's
  `ReaderAccessController::isAuthorizedForChapter`); wrapped copies travel alongside the
  ciphertext in the same file.
- A minimal "register a reader" step: writer pastes a new reader's public key + a name,
  `AccessRegistry::appendReader` writes it, and this action commits that (plaintext, no
  secrets) change.
- Defining the on-disk encrypted-chapter file format (`ChapterFile`), since both this
  feature and spec 004's reader-side decryption must agree on it.

**Out of scope (future features):**
- Retroactive re-wrapping: removing/adding a reader only affects chapters encrypted
  after that change — past chapters aren't touched (see Edge Cases).
- A reader keypair *generation* tool for this side — readers generate their own RSA
  keypair (spec 004 handles this on the reader side) and hand the writer only their
  public key.
- A GUI — console-based `WriterMenu` only.

## Behavior / Functional Requirements
1. **Load registry:** `AccessRegistry::loadFromFile` reads `access.txt` from the repo
   root. Each line: `<reader-name>:<base64 RSA public key, SPKI/DER>:<level>:<chapters>`
   (spec 003's 4-field format — this spec never sees the older 2-field format). Missing/
   empty file → treat as zero readers; warn the writer nothing will be decryptable yet
   and offer to add a reader now or proceed anyway (explicit choice, never silent).
2. **Register a reader (optional, before encrypting):** prompt "Add a reader? (y/n)".
   If yes: ask for a display name and the reader's public key (pasted in), then delegate
   to spec 003's `ReaderAccessController` to collect and validate the level/chapter-list,
   then `AccessRegistry::appendReader` writes the resulting 4-field line, then this
   action runs `git add access.txt`, `git commit -m "chore: add reader <name> to
   access.txt"`, `git push` directly (via the shared `GitProcess` helper from spec
   001 — this action does not re-invoke `GitBootstrapService`; it only reuses the
   `origin` remote and upstream tracking that spec 001 already set up once).

   **Precondition:** this menu item is only reachable after spec 001's bootstrap has
   completed successfully in this folder (git initialized, `origin` configured, upstream
   tracking set) — it performs no first-time git setup itself.
3. **Prompt for the encryption password** once per invocation of this menu item — not
   persisted, not cached beyond the running process — reused for every chapter processed
   in that invocation.
4. **Derive the AES key:** `CryptoProvider::deriveKey` — PBKDF2-HMAC-SHA256 (high
   iteration count) from the password, with a random per-chapter salt.
5. **Encrypt the chapter:** `CryptoProvider::encrypt` — AES-256-GCM, random per-chapter
   nonce/IV, over the full chapter content (title + body — the whole thing becomes
   ciphertext).
6. **Wrap the AES key per reader:** for every reader in `AccessRegistry` authorized for
   this chapter number (per spec 003's `ReaderAccessController::isAuthorizedForChapter`),
   `CryptoProvider::wrapKey` RSA-OAEP-encrypts the AES key with that reader's public key.
7. **Write the output file** (`ChapterFile`, format below), then `git add` that file,
   commit (e.g. `chore: encrypt chapter <N>`), and `git push` — same direct-`GitProcess`
   approach as step 2, reusing the `origin`/tracking spec 001 already configured.
   Plaintext is never staged, committed, or pushed.
8. **Confirm outcome:** print which chapter was encrypted, for how many readers, and
   remind the writer plaintext only lives in the local gitignored working folder. Return
   to `WriterMenu`.

## Data Model / API / CLI Surface
- **Class:** `ChapterEncryptionService`, reached via `WriterMenu`'s "Encrypt a chapter"
  item, any time after spec 001's bootstrap has already completed in this folder — it
  owns its own add/commit/push for `access.txt` and `.enc` file changes (see Behavior
  steps 2 and 7); it does not depend on the bootstrap running again.
- **`access.txt`** (repo root, git-tracked, plaintext, no secrets), parsed/written only
  by `AccessRegistry`, always in spec 003's 4-field format:
  ```
  alice:MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA...:Master:
  bob:MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA...:Apprentice:2,3
  ```
- **Encrypted chapter file location/naming (pinned convention — spec 004's reader relies
  on this exact pattern):** a git-tracked `chapters/` folder at the repo root, one file
  per chapter, named `chapter-<NN>.enc` with a 2-digit zero-padded chapter number (e.g.
  `chapters/chapter-01.enc`, `chapters/chapter-12.enc`). `ChapterFile`'s JSON envelope:
  ```json
  {
    "salt": "base64...",
    "iv": "base64...",
    "ciphertext": "base64...",
    "wrappedKeys": { "alice": "base64...", "bob": "base64..." }
  }
  ```
- **Plaintext source:** a gitignored working folder (e.g. `chapters-source/`,
  gitignored automatically by spec 001's bootstrap step) — never staged or committed.
- **Sensitive value handling — flag for `security-reviewer` / `security-review`:**
  - Password: masked prompt, exists only in memory for that process, never logged or
    written to disk.
  - AES content key: memory-only; never persisted unwrapped — only its per-reader
    RSA-wrapped form is written to the output file.
  - Reader private keys are never touched by this feature — only public keys ever
    reach the writer's machine/repo.
  - `CryptoProvider` wraps a vetted crypto library (OpenSSL) — no hand-rolled AES/RSA/
    PBKDF2, and it's the same class instance/parameters spec 004's reader uses to
    decrypt, so there's no separate parameter set to keep in sync.

## Edge Cases & Error Handling
- `.git` not present, or no `origin` remote configured → clear error telling the
  writer to run spec 001's bootstrap first; not a crash, no attempt to perform
  first-time git setup itself.
- `access.txt` missing/empty → explicit warning + offer to add a reader now, never
  silently produced as undecryptable.
- Reader added after a chapter was already encrypted → that chapter is NOT
  retroactively re-wrapped for them; expected, documented behavior, not a bug.
- Malformed line in `access.txt` → `AccessRegistry` skips it with a warning naming the
  line number; don't abort the whole run over one bad entry.
- Duplicate reader name → `AccessRegistry::appendReader` warns, refuses to silently
  overwrite; ask the writer to resolve.
- Wrong password or tampered ciphertext (reader-side, spec 004) → AES-GCM's
  authentication tag fails verification → clear decryption failure, never silently
  return garbage plaintext.
- Blank password entered → re-prompt, never proceed with an empty password.

## Acceptance Criteria
1. Plaintext chapter + password + `access.txt` listing 2 authorized readers → produces
   one `.enc` file with ciphertext plus exactly 2 wrapped-key entries; no plaintext
   anywhere in output or git-staged files.
2. Decrypting with the correct password and matching private key recovers
   byte-identical original content (round-trip correctness, testable without the real
   reader flow by using a test keypair against `CryptoProvider` directly).
3. A reader's wrapped-key entry is only unwrappable with that reader's own matching
   private key — verified by attempting "alice"'s entry with "bob"'s private key and
   confirming failure.
4. Tampering with one byte of `ciphertext` post-encryption causes decryption to fail
   loudly (GCM auth-tag mismatch), never silently returns corrupted plaintext.
5. Missing/empty `access.txt` prompts the writer rather than silently producing a
   zero-reader file.
6. No password, AES key, or private key ever appears in console output, any committed
   file, or process logs.

## Open Questions
- `access.txt` line format and the `.enc` JSON envelope shape above are proposed
  defaults, not yet confirmed line-by-line with the user — flag for revision.
- Exact PBKDF2 iteration count / AES-GCM / RSA-OAEP parameters are left for
  implementation time, to be signed off by `security-reviewer` per `CLAUDE.md`
  convention rather than fixed in this PRD.
- Whether the writer should automatically be their own "reader" entry (so they can
  reopen past chapters without keeping plaintext forever) is undecided — current
  design assumes the writer always keeps the original plaintext locally.
