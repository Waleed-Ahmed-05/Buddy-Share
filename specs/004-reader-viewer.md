# 004 — Reader-Side Chapter Viewer

## Summary
The Reader-role behavior of `BuddyShare.exe`: view whichever encrypted chapters a writer
has granted access to — fetching `access.txt` and encrypted chapter files directly from
GitHub, decrypting locally with the reader's own RSA private key.

## Motivation / Problem
Specs 002/003 built the writer-side encryption and access-control model but explicitly
deferred the consuming side. Without this, an authorized reader has no way to actually
turn an `.enc` file back into readable prose. This feature closes that loop.

## Architecture Context
This spec's behavior is reached when spec 001's `RoleManager` dispatches a launch into
Reader mode. It owns:
- **`ChapterFetcher`** (`src/reader/ChapterFetcher.{h,cpp}`) — fetch/list/verify-
  decryptability logic (steps 3, 5, 6 below), via the shared `GitHubClient` (also used by
  spec 001's writer-side repo-validation call).
- **`ReaderViewerService`** (`src/reader/ReaderViewerService.{h,cpp}`) — keypair
  management, decryption, and the `ReaderMenu` display loop (steps 1, 2, 7-9 below).
- Uses, but does not own, the shared classes defined once for both roles:
  - **`AccessRegistry`** (`src/shared/AccessRegistry.{h,cpp}`, spec 002) — the same
    parser used writer-side, here loaded from a network fetch instead of a local file;
    `AccessRegistry::findByName` (O(1), `std::unordered_map`-backed) does the reader's
    own-entry lookup (step 4).
  - **`ChapterFile`** (`src/shared/ChapterFile.{h,cpp}`, spec 002) — the same `.enc`
    envelope definition, read here instead of written.
  - **`CryptoProvider`** (`src/shared/CryptoProvider.{h,cpp}`, spec 002) — the same
    RSA-OAEP/AES-256-GCM implementation used to encrypt is used here to decrypt,
    guaranteeing parameter alignment by construction (no separate library/parameter set
    to keep in sync, since it's the same class in the same binary).

## User Stories
- As a reader, I want to enter the writer's GitHub details and my own username, and see
  a menu of exactly the chapters I'm allowed to read.
- As a first-time reader, I want the program to generate my keypair for me and show me
  the public key I need to send the writer, without needing to understand RSA myself.
- As a reader, I want a chapter I pick to just show up as readable text, with no
  password to remember or enter.
- As a reader, I want a clear message (not a crash) if I'm not registered yet, or if
  a chapter I nominally have permission for isn't actually decryptable yet.

## Scope
**In scope:**
- First-run RSA keypair generation per username, protected local private-key storage,
  and displaying the public key for manual/out-of-band delivery to the writer.
- Fetching `access.txt` and chapter `.enc` files from a public GitHub repo via the
  Contents API — no git, no token, no winget dependency on the reader's machine (this
  role never touches `GitBootstrapService`).
- Parsing the reader's own `access.txt` entry (spec 003's 4-field format, via the shared
  `AccessRegistry`) to determine their level and allowed chapter numbers.
- Cross-checking allowed chapters against which chapters actually exist in the repo,
  and against which chapters' `.enc` files actually include this reader's wrapped key
  (access.txt permission and real ciphertext access can be out of sync per specs
  002/003's own "no retroactive re-wrap" rule) — the menu reflects real decryptability.
- RSA-OAEP unwrap of the AES key + AES-256-GCM decryption + display of the selected
  chapter's plaintext content.

**Out of scope:**
- Any write/push capability back to the repo (registration stays manual/out-of-band
  via spec 003, run by the writer).
- Private-key rotation/recovery if a reader loses their local key (inherits spec
  003's already-flagged open question).
- A GUI — console-based `ReaderMenu` only.
- **Private target repos are unsupported — the writer's GitHub repo must be
  public for this role to work at all.** There is no token support anywhere in this
  feature; a private repo means every Contents API call in step 3/6 returns 404
  indistinguishably from "doesn't exist," so this is a hard requirement, not a
  planned-later capability (see the matching note in spec 001).

## Behavior / Functional Requirements
1. **Launch prompts, in order (every run — not one-time like spec 001's bootstrap):**
   writer's GitHub username → repo name → reader's own username.
2. **Keypair check (`ReaderViewerService::ensureLocalKeypair`):** look for a locally
   stored keypair for this username. If none exists, generate an RSA keypair now, store
   the private key protected on this machine, and print the public key with instructions
   to send it to the writer. Continue to step 3 regardless (they may already have sent a
   key from a prior run or another machine).
3. **Fetch `access.txt`** (`ChapterFetcher::fetchAccessTxt`) via the GitHub Contents API
   for `{writer}/{repo}`, populating the same `AccessRegistry` type used writer-side.
   Network/repo-not-found errors → clear message, return to launch prompts.
4. **Look up the reader's own line** via `AccessRegistry::findByName`.
   - Not found → "You're not registered with this writer yet — send them this public
     key: `<pub key>`" (re-displaying it even if step 2 didn't just generate it), return
     to launch prompts.
   - Malformed line → clear error, return to launch prompts (don't guess at intent).
   - Found → parse level + chapter list per spec 003's format.
5. **Determine candidate chapters (`ChapterFetcher::listMasterCandidates` /
   Apprentice-Novice path):**
   - Master → list the `chapters/` folder via the Contents API
     (`GET /repos/{owner}/{repo}/contents/chapters`); every entry matching
     `chapter-<NN>.enc` (per spec 002's pinned naming convention) is a candidate,
     parsed once into a sorted `std::vector<int>` of chapter numbers (reused as the
     menu's iteration order in step 7) — this directory listing already confirms each
     one exists, so there's no 404 case for Master. An empty/missing `chapters/` folder
     (no chapters published yet) just means zero candidates, not an error.
   - Apprentice/Novice → their own listed chapter number(s) from `access.txt` are
     the only candidates; existence isn't known yet and is checked per-candidate
     in step 6 (no directory listing needed for these levels).
6. **Verify real decryptability (`ChapterFetcher::tryFetchChapter`):**
   - For each Master candidate (already confirmed to exist via the step 5 listing):
     fetch it and check the reader's username is present in its `wrappedKeys`.
     Present → shown as readable; absent → shown as "pending — not yet available".
   - For each Apprentice/Novice candidate chapter number `N`: fetch
     `chapters/chapter-<NN>.enc` (2-digit zero-padded) directly via the Contents
     API.
     - 404 → chapter isn't published yet → "pending — not yet available" (not
       hidden, not an error).
     - 200 → check the reader's username is present in `wrappedKeys`. Present →
       readable; absent → also "pending — not yet available" (access.txt nominally
       allows it, but the actual file doesn't include this reader's key yet, per
       specs 002/003's "no retroactive re-wrap" rule).
7. **Menu (`ReaderMenu`):** list readable chapters by number; reader selects one (or
   exits back to `RoleManager`/quits the program — Reader mode has no other menu items).
8. **Decrypt selected chapter (`ReaderViewerService`):** RSA-OAEP-decrypt the reader's
   `wrappedKeys` entry (using their local private key, via `CryptoProvider`) to recover
   the AES key, then AES-256-GCM-decrypt `ciphertext` using that key + the file's
   `iv`/salt. Auth-tag failure → clear decryption-failure message, never partial/garbage
   output.
9. **Display** the decrypted chapter content in the console; return to the menu
   (steps 7-8) until the reader chooses to exit.

## Data Model / API / CLI Surface
- **Executable:** part of `BuddyShare.exe` (see spec 001) — no separate binary. Reader-
  only code (`ChapterFetcher`, `ReaderViewerService`, `ReaderMenu`) still compiles into
  the same CMake target as writer-only code; only `RoleManager`'s dispatch decides which
  path a given install ever reaches.
- **Network:** GitHub Contents API only (`GET /repos/{owner}/{repo}/contents/{path}`),
  no authentication — the target repo must be public (see Scope), via the shared
  `GitHubClient` class.
- **Local private key storage:** `%APPDATA%\BuddyShare\keys\<username>_private.pem`,
  protected via Windows DPAPI — never leaves the machine, never transmitted anywhere.
  (Consolidated under the same `%APPDATA%\BuddyShare\` app folder as spec 001's
  `.role` lock file, now that writer and reader are one product.)
- **Libraries (shared with the writer side via `CryptoProvider`/`GitHubClient` — flag
  for `cpp-reviewer`/`security-reviewer` confirmation at implementation time):**
  - HTTP: WinHTTP (native Windows API, no extra dependency) over libcurl, given the
    project is Windows-targeted throughout (winget, the writer's git bootstrap).
  - Crypto: OpenSSL (widely vetted, available via CMake `FetchContent`/vcpkg) for
    RSA-OAEP and AES-256-GCM, via `CryptoProvider` — used identically by the writer's
    encryption (spec 002) and this feature's decryption, so there's no cross-language
    parameter alignment risk to track anymore.
- **Sensitive value handling — flag for `security-reviewer`/`security-review`:**
  - Private key: generated locally, stored DPAPI-protected, never logged, never sent
    anywhere — only the *public* key is ever intentionally displayed/transmitted.
  - Decrypted chapter content and the AES key exist only in memory during display.

## Edge Cases & Error Handling
- Writer/repo doesn't exist, or network failure → clear error, no crash.
- Reader username not in `access.txt` → clear "not registered" message + their
  public key to send, not a crash or silent empty menu.
- `access.txt` line malformed for this reader → clear error, don't guess.
- Chapter permitted by `access.txt` but not yet present in the repo, or present but
  missing this reader's wrapped-key entry → shown as pending/unavailable in the menu,
  not hidden and not a hard error.
- Corrupted/tampered ciphertext, or a private key that doesn't match what the writer
  registered → GCM auth-tag failure surfaces as a clear decryption error.
- Local private key file missing/corrupted after having previously registered a
  public key with the writer → the program would generate a *new* keypair, whose
  public key no longer matches what's in `access.txt`; decryption then fails for every
  chapter until the writer updates `access.txt` with the new public key (inherits spec
  003's already-flagged key-rotation gap — surfaced here as a concrete symptom of it).
- GitHub API unauthenticated rate limits — acceptable for expected small-scale/
  personal use; flagged as a scaling limitation, not solved here.

## Acceptance Criteria
1. First-ever run for a new username generates a local keypair and clearly displays
   the public key with instructions.
2. A subsequent run for the same username reuses the existing key — no regeneration.
3. A username not yet in `access.txt` gets a clear "not registered" message (with
   their public key shown again), no crash.
4. A registered reader's menu lists exactly the chapters that are both nominally
   permitted (`access.txt`) and actually wrapped-key-included in the real `.enc`
   file — no more, no less; others show as pending rather than being silently
   omitted.
5. Selecting a valid chapter decrypts to byte-identical original content (round-trip
   correctness against a known test fixture — keypair + `.enc` file produced by a
   test harness calling `CryptoProvider` directly, not requiring the real writer flow).
6. Tampered ciphertext or a mismatched private key produces a clear decryption-
   failure message, never garbage/corrupted output.
7. No private key material, AES key, or decrypted plaintext ever appears in logs —
   only ever the reader's own *public* key is intentionally shown.

## Open Questions
- HTTP library (WinHTTP proposed) and crypto library (OpenSSL proposed) choices are
  defaults pending `cpp-reviewer`/`security-reviewer` confirmation, not locked in.
- Exact RSA-OAEP/AES-GCM parameter alignment is now guaranteed by sharing one
  `CryptoProvider` instance/class between encrypt and decrypt paths — no longer a
  cross-language interop risk as it was when writer/reader were separate PowerShell/C++
  programs, but implementation should still confirm the shared class is actually used
  by both sides rather than accidentally duplicated.
- Private-key loss/rotation recovery flow remains unresolved (inherited from spec 003).
- Post-chapter-read behavior (return to menu vs. exit) assumed as "return to menu
  until reader exits" — minor UX default, easy to revise.
