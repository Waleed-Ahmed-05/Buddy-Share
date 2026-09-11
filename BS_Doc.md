# BuddyShare Documentation

**Last Updated:** 2026-09-11  
**Features Complete:** Spec 001 (Role Selection & Git Bootstrap), Spec 002 (Chapter Encryption), Spec 003 (Reader Access Levels), Spec 004 (Reader-Side Chapter Viewer), Spec 005 (CLI Output Styling)  
**Recent Fixes & Enhancements:** Silent Git-Failure Detection, Honest GitHub Rate-Limit Handling, Improved Prompt Text for First-Time Users, Multi-Entry .gitignore Seeding, CLI Color & Status-Line Styling

## Overview

BuddyShare is a single C++ executable (`BuddyShare.exe`) that serves both Writers and Readers for the L.I.F.E Chapter Sharing Tool. This document covers implementations from Spec 001 (role selection and git bootstrap), Spec 002 (chapter encryption), Spec 003 (reader access levels and chapter assignment), Spec 004 (reader-side chapter viewing and decryption), and Spec 005 (CLI output styling with color and status lines).

## Table of Contents

1. [Getting Started](#getting-started)
2. [Architecture](#architecture)
3. [Features](#features)
   - [Feature 1: Role Selection](#1-one-time-role-selection-rolemanager)
   - [Feature 2: Git Bootstrap](#2-one-time-git-bootstrap-gitbootstrapservice--writer-role-only)
   - [Feature 3: Chapter Encryption](#3-chapter-encryption-chapterencryptionservice--writer-role-only)
   - [Feature 4: Reader Access Levels](#4-reader-access-levels--chapter-assignment-readeraccesscontroller--writer-role-only)
   - [Feature 5: Reader Chapter Viewer](#5-reader-chapter-viewer-chapterfetcher--readerviewerservice--reader-role-only)
   - [Defect Fix: Reader Access Edits Now Commit & Push](#6-defect-fix-reader-access-edits-now-commit--push-spec-003)
   - [Defect Fix: Silent Git-Failure Detection](#7-defect-fix-silent-git-failure-detection-2026-09-11)
   - [Enhancement: Honest GitHub Rate-Limit Handling](#8-enhancement-honest-github-rate-limit-handling-2026-09-11)
   - [Enhancement: Improved Prompt Text for First-Time Users](#9-enhancement-improved-prompt-text-for-first-time-users-2026-09-11)
   - [Enhancement: Multi-Entry .gitignore Seeding](#10-enhancement-multi-entry-gitignore-seeding-2026-09-11)
   - [Feature 5: CLI Output Styling](#11-cli-output-styling-spec-005)
4. [Build Instructions](#build-instructions)
5. [Usage](#usage)
6. [Security & Caveats](#security--caveats)

---

## Getting Started

### For Writers

On your first run of `BuddyShare.exe`:

1. You'll be prompted: **"Are you the Writer or a Reader? [W/R]"**
   - Type `W` or `w` to lock in the Writer role
   - This choice is permanent and saved locally
   
2. After role selection, the Writer Menu appears with available options:
   - **"1) Initialize repo"** — one-time git bootstrap (spec 001)
   - **"0) Exit"** — exit the program

3. Choose "1" to bootstrap your git repository:
   - Validates your GitHub repo exists and is empty
   - Initializes git in your current folder
   - Creates `.gitignore` with `chapters-source/` and `*.exe` (so plaintext sources and built executables don't get committed)
   - Sets your git identity (username and email)
   - Makes the initial commit
   - Links your local repo to GitHub
   - Pushes your code to the remote
   
4. After bootstrap completes successfully:
   - "Initialize repo" disappears from the menu (it's a one-time action)
   - Additional options appear: "Encrypt a chapter" (spec 002) and "Manage reader access" (spec 003)
   - The Writer Menu now serves as the hub for all chapter and access management

### For Readers

On your first run of `BuddyShare.exe`:

1. You'll be prompted: **"Are you the Writer or a Reader? [W/R]"**
   - Type `R` or `r` to lock in the Reader role
   - This choice is permanent and saved locally

2. On every subsequent run, you'll be prompted for:
   - **Writer's GitHub username** (e.g., "alice")
   - **Repository name** (e.g., "my-chapters")
   - **Your username** (e.g., "bob" — must match what the writer has registered in `access.txt`)

3. First-time setup (one-time per username):
   - If you've never logged in with this username before, BuddyShare generates a local RSA keypair for you
   - **Important:** The program displays your public key on screen with instructions to send it to the writer
   - The private key is stored securely on your machine (protected via Windows DPAPI) and never leaves it

4. Chapter menu:
   - After registration validation, you'll see a menu of chapters you're allowed to read
   - Chapters show their status: **readable** (you can decrypt it now) or **pending** (not yet published by the writer, or their wrapped key isn't ready)
   - Select a chapter number to view its decrypted content
   - After viewing, return to the menu to pick another chapter or exit

5. Repeat the process:
   - Return to launch prompts to view chapters from a different writer, or exit the program

---

## Architecture

### Single Executable, Multiple Roles

```
BuddyShare.exe
├── Role Selection (RoleManager)
│   └── First-ever launch: prompt and persist role
│       └── Subsequent launches: dispatch straight to role's menu
├── Writer Role (WriterMenu)
│   ├── Initialize repo (spec 001)
│   ├── Encrypt chapters (spec 002)
│   │   ├── ChapterEncryptionService — orchestrates encryption workflow
│   │   ├── CryptoProvider — PBKDF2/AES-256-GCM/RSA-OAEP (OpenSSL-backed)
│   │   └── ChapterFile — .enc JSON envelope format
│   ├── Register readers (during encryption)
│   │   └── AccessRegistry — 4-field reader/key/level/chapters registry
│   └── Manage reader access (spec 003)
│       └── ReaderAccessController — level assignment and chapter authorization
└── Reader Role (spec 004)
    ├── Launch prompts → GitHub repo validation → keypair management
    ├── Fetch access.txt and chapter files via GitHub Contents API
    ├── ChapterFetcher — list/verify chapter availability
    ├── ReaderViewerService — keypair management and decryption
    ├── ReaderMenu — display readable/pending chapters
    └── CryptoProvider — RSA-OAEP unwrap + AES-256-GCM decrypt (shared with writer)
```

### Key Classes

| Class | Purpose | Location |
|-------|---------|----------|
| `RoleManager` | First-run role prompt; loads persisted role; dispatches to correct menu | `src/core/RoleManager.*` |
| `RoleStore` | Reads/writes hidden `.role` file at `%APPDATA%\BuddyShare\.role` | `src/core/RoleStore.*` |
| `GitBootstrapService` | Validates GitHub repo; runs git init → commit → push sequence | `src/writer/GitBootstrapService.*` |
| `WriterMenu` | Menu loop for Writer role; manages available commands based on state (spec 001-003) | `src/writer/WriterMenu.*` |
| `ChapterEncryptionService` | Orchestrates "Encrypt a chapter" workflow: prompts password, wraps keys per reader, writes `.enc` files, commits/pushes | `src/writer/ChapterEncryptionService.*` |
| `CryptoProvider` | PBKDF2-HMAC-SHA256, AES-256-GCM, RSA-OAEP; shared by writer (encrypt) and reader (decrypt) | `src/shared/CryptoProvider.*` |
| `ChapterFile` | Defines `.enc` JSON envelope (salt, iv, ciphertext, wrappedKeys); serializes/parses to/from disk | `src/shared/ChapterFile.*` |
| `AccessRegistry` | Parser/writer for `access.txt` (4-field format: name, base64 RSA public key, level, chapters); O(1) lookup | `src/shared/AccessRegistry.*` |
| `ReaderAccessController` | Prompts for reader access levels and chapter lists; authorization checks during encryption | `src/writer/ReaderAccessController.*` |
| `SystemGitProcess` | Wrapper over system `git` executable; used by bootstrap and encryption | `src/writer/SystemGitProcess.*` |
| `HttpGitHubClient` | Makes REST calls to GitHub API (repo validation, branch detection, contents listing) | `src/shared/HttpGitHubClient.*` |
| **Reader-Side Classes** | | |
| `ChapterFetcher` | Fetches `access.txt` and chapter `.enc` files from GitHub Contents API; lists candidates; verifies decryptability | `src/reader/ChapterFetcher.*` |
| `ReaderViewerService` | Manages reader's local RSA keypair (generation, storage, display); decrypts chapters via RSA-OAEP unwrap + AES-GCM | `src/reader/ReaderViewerService.*` |
| `ReaderMenu` | Displays chapter list with readable/pending status; tracks selectable chapters | `src/reader/ReaderMenu.*` |
| `DpapiPrivateKeyStore` | Stores reader's private key at `%APPDATA%\BuddyShare\keys\<username>_private.pem` (DPAPI-protected) | `src/reader/DpapiPrivateKeyStore.*` |
| `IPrivateKeyStore` | Interface for private-key storage (testability abstraction) | `src/reader/ReaderViewerService.h` |

### Role Lock Mechanism

The role is persisted to a hidden file at `%APPDATA%\BuddyShare\.role`:

```
%APPDATA%\BuddyShare\.role
├── Content: plain text ("writer" or "reader")
├── Hidden: Windows hidden attribute set
└── Read on every launch; never modified after first write
```

This file is **not encrypted**—it's hidden only to discourage casual tampering, not to resist deliberate attack. The choice is machine-local and permanent: once locked in, there is no in-program way to switch roles.

---

## Features

### 1. One-Time Role Selection (RoleManager)

**What it does:**
- On first launch, prompts: "Are you the Writer or a Reader? [W/R]"
- Re-prompts if you enter anything other than W/R
- Writes your choice to a hidden `.role` file in `%APPDATA%\BuddyShare\`
- On all future launches, reads this file and skips the prompt entirely

**Why:** So you never accidentally slip into the wrong role's workflow, and so you don't have to answer every time.

### 2. One-Time Git Bootstrap (GitBootstrapService) — Writer Role Only

**What it does:**

1. **Check if already initialized:**
   - If `.git` already exists in your current folder, skip bootstrap entirely (no prompts, no git commands)
   - This prevents accidental re-initialization

2. **Ensure git is available:**
   - Runs `git --version`
   - If missing, attempts to install via `winget install --id Git.Git`
   - If that fails, shows manual install instructions and returns to menu

3. **Seed `.gitignore`:**
   - Creates (or appends to) `.gitignore` with two entries: `chapters-source/` and `*.exe`
   - `chapters-source/` is **required**—spec 002 depends on plaintext source files being gitignored
   - `*.exe` prevents built executables (e.g., `BuddyShare.exe`, `tests/buddyshare_tests.exe`) from being accidentally committed to the repo
   - Each entry is checked independently: if an entry already exists (from a prior run or user hand-edit), it is not duplicated; any still-missing entries are appended
   - Existing file content is never removed; new entries are only appended with a leading newline if needed

4. **Prompt for GitHub details** (in order, nothing saved to disk):
   - **GitHub username**
   - **Email address** (for git commits)
   - **Repository name** (can be a bare name, or a full URL—normalized automatically)
   - **Personal Access Token (optional)** — masked input; blank = skip

5. **Validate the repository:**
   - Calls GitHub API to check if `{username}/{repo}` exists
   - Verifies it's empty (has no commits)
   - Captures the repository's actual default branch (`main`, `master`, or custom)

6. **Initialize git and push:**
   ```
   git init
   git branch -M {branch}                              # rename to match remote default
   git config --local user.name "{username}"
   git config --local user.email "{email}"
   git add .
   git commit -m "Initial commit"
   git remote add origin https://github.com/{username}/{repo}.git
   git push ...                                        # see below
   git branch --set-upstream-to=origin/{branch} ...   # (if using token)
   ```
   
   - **With token:** uses the token for this one push, then never stores it
   - **Without token:** relies on your system's Git Credential Manager (cached credentials, etc.)

7. **Confirm success:**
   - Prints the repo URL, branch name, and tracking status
   - Returns to the menu; "Initialize repo" is now hidden

### 3. Chapter Encryption (ChapterEncryptionService) — Writer Role Only

**What it does:**

Encrypts plaintext chapters with AES-256-GCM (password-derived key) and wraps the content key once per authorized reader using their RSA public key. No plaintext reaches git — only ciphertext is committed and pushed. Readers can decrypt independently using their own private keys, without ever learning the writer's password.

**Workflow:**

1. Writer selects "Encrypt a chapter" from the Writer Menu (only available after spec 001's bootstrap completes)

2. **Precondition check:** 
   - Verifies `.git` directory exists and `origin` remote is configured
   - If not, returns to menu with an error (this isn't a crash; no attempt to perform bootstrap itself)

3. **Check reader registry:**
   - If `access.txt` is missing/empty, warns the writer that no readers are registered yet
   - Always offers: "Add a reader? (y/n)" — never silently proceeds with zero readers (per Acceptance Criterion 5)
   - If yes: prompts for reader's display name and public key (pasted in), then delegates to `ReaderAccessController` to collect level/chapters, then `AccessRegistry::appendReader` writes the entry, and the program automatically commits and pushes it (e.g., `chore: add reader alice to access.txt`)

4. **Encrypt:**
   - Prompts for encryption password (masked input; re-prompts on blank)
   - Derives a 32-byte AES-256 key using PBKDF2-HMAC-SHA256 with a random salt and high iteration count
   - Reads plaintext from `chapters-source/chapter-<NN>.txt` (or specified path)
   - Encrypts with AES-256-GCM using a random per-chapter IV
   - For each reader in `access.txt` authorized for this chapter (via `ReaderAccessController::isAuthorizedForChapter`):
     - Wraps the AES key using the reader's RSA public key (RSA-OAEP)
     - Stores wrapped key in the output file
   - **Caveat:** If a reader's stored public key is malformed (fails RSA parse), that reader is skipped with a console warning; encryption continues for other readers (see `EncryptChapterOutcome::some_readers_skipped`)

5. **Write and commit:**
   - Writes `chapters/chapter-<NN>.enc` (2-digit zero-padded filename, e.g., `chapter-01.enc`)
   - JSON format: `{ salt: "base64...", iv: "base64...", ciphertext: "base64...", wrappedKeys: { "alice": "base64...", "bob": "base64..." } }`
   - `git add` the `.enc` file, commits with message like `"chore: encrypt chapter 1"`, pushes directly
   - **Caveat:** A zero-reader chapter (empty `wrappedKeys` map) is allowed — ciphertext is still written, just not decryptable until you add readers

6. **Confirm outcome:**
   - Prints which chapter was encrypted, for how many readers
   - Reminds writer that plaintext only lives in the local gitignored `chapters-source/` folder
   - Returns to Writer Menu

**Data Model:**

- **Input:** plaintext file (e.g., `chapters-source/chapter-01.txt`)
- **Output:** `chapters/chapter-01.enc` (JSON with base64-encoded fields)
- **Registry:** `access.txt` (4-field format; writer registers readers before or during encryption)
- **Cryptography:**
  - Key derivation: PBKDF2-HMAC-SHA256 (high iteration count, random salt)
  - Symmetric encryption: AES-256-GCM with random IV, 16-byte GCM auth tag appended
  - Key wrapping: RSA-OAEP per authorized reader
  - All byte fields in output are base64-encoded

**Integration with Access Levels:**

After a reader is registered (via `ReaderAccessController`), that reader's level and chapter list determine which chapters get their key wrapped:
- **Master:** wrapped for every chapter, past and future
- **Apprentice:** wrapped only for chapters in their list
- **Novice:** wrapped only for their single assigned chapter

A reader registered *after* a chapter is encrypted does **not** retroactively get added to that chapter's wrapped keys — only future chapters include the new reader. This is intentional (spec 002 Edge Cases).

**Caveats:**

- **Password exists only in RAM:** Never logged, never written to disk; unique to this invocation
- **Plaintext never touches git:** Source files live in `.gitignore`'d `chapters-source/`; plaintext is never staged, committed, or pushed
- **Blank password is rejected:** Re-prompts on empty entry
- **Malformed public keys are tolerated:** If a reader's stored key fails to parse, that reader is skipped with a warning; the chapter is still encrypted and pushed, just missing that reader's wrapped key
- **No retroactive re-wrapping:** Adding or removing a reader doesn't change past chapters; changes apply only to new encryptions

### 4. Reader Access Levels & Chapter Assignment (ReaderAccessController) — Writer Role Only

**What it does:**

After writers have registered readers (spec 002's process), they can manage each reader's access level and chapter list via the "Manage reader access" menu item. The access control system supports three levels:

1. **Master:** Reader gets access to all chapters, past and future (no chapter list needed)
2. **Apprentice:** Reader gets access to specific chapters listed by the writer (one or more)
3. **Novice:** Reader gets access to exactly one chapter (useful for previews)

Each reader is stored in `access.txt` as a 4-field line: `<name>:<base64-public-key>:<level>:<chapters>`

**Workflow:**

1. Writer selects "Manage reader access" from the Writer Menu
2. Writer chooses an existing reader's name to edit
3. Writer is prompted for a new level (Master/Apprentice/Novice)
   - If **Apprentice**: prompted for chapter numbers (comma-separated, e.g., `2,3`)
   - If **Novice**: prompted for exactly one chapter number (e.g., `1`)
   - If **Master**: no further prompt
4. The reader's entry is updated in-place (name and public key unchanged)
5. **Git commit and push** (automatic): After validation succeeds, the program:
   - Stages `access.txt` with `git add access.txt`
   - Commits with the message: `chore: update access for <name> to <level>` (e.g., `chore: update access for alice to Apprentice`)
   - Pushes to the remote repository with `git push`
   - This ensures that any reader (e.g. via spec 004) fetching `access.txt` from the remote repo will see the updated access level immediately

**Data in `access.txt`:**

```
alice:MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA...:Master:
bob:MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA...:Apprentice:2,3
carol:MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA...:Novice:1
```

**Encryption Impact (spec 002 integration):**

When encrypting a chapter, `ReaderAccessController::isAuthorizedForChapter()` is called per reader to decide if their wrapped key should be included:
- **Master** readers: always included for every chapter
- **Apprentice** readers: included only if the chapter number is in their list
- **Novice** readers: included only if the chapter number matches their single chapter

Readers can reference chapters that don't exist yet (e.g., pre-authorizing chapter 5 before it's written) — this is allowed, not an error.

### 5. Reader Chapter Viewer (ChapterFetcher, ReaderViewerService) — Reader Role Only

**What it does:**

Reads encrypted chapters published by a writer, without the writer's password. Readers authenticate via their own RSA keypair (generated locally on first use) and decrypt chapters independently. This spec implements the complete Reader flow: keypair setup, chapter listing/validation, decryption, and chapter display.

**Workflow:**

1. **Launch prompts** (every run — not one-time like the writer's bootstrap):
   - Writer's GitHub username (e.g., "alice")
   - Repository name (e.g., "my-chapters")
   - Your own username (e.g., "bob" — must match what the writer registered in `access.txt`)

2. **Keypair check (`ReaderViewerService::ensure_local_keypair`)**:
   - Looks for an existing RSA private key stored locally for your username
   - If this is your **first time** with this username:
     - Generates a new 2048-bit RSA keypair
     - Stores the private key securely at `%APPDATA%\BuddyShare\keys\<username>_private.pem` (DPAPI-protected)
     - **Displays your public key** with instructions: "Send this to the writer so they can wrap your chapters"
     - Never shows or logs the private key itself
   - If you've used this username before:
     - Reuses the existing key (no regeneration)
   - Either way, the program continues to step 3 (you may already be registered, or the writer may be adding you now)

3. **Fetch `access.txt`** (`ChapterFetcher::fetch_access_txt`):
   - Fetches the file from `{writer}/{repo}` via the GitHub Contents API (no authentication token needed; repo must be public)
   - Network error or repo not found → clear message, returns to launch prompts

4. **Look up your own entry** (`AccessRegistry::find_by_name`):
   - Searches for your username in `access.txt`
   - **Not found** → displays your public key again with "You're not registered yet — send this to the writer", returns to launch prompts
   - **Found & malformed** → clear error message, returns to launch prompts (doesn't guess at intent)
   - **Found & valid** → parses your level (Master/Apprentice/Novice) and chapter list

5. **Determine candidate chapters** (`ChapterFetcher::list_master_candidates` for Master, or your explicit list for Apprentice/Novice):
   - **Master readers**: lists the `chapters/` folder via GitHub Contents API; every file matching `chapter-<NN>.enc` becomes a candidate
   - **Apprentice/Novice readers**: your listed chapter numbers are the only candidates; no directory listing needed
   - Master candidates are checked for existence (already confirmed by the folder listing); Apprentice/Novice candidates are verified per-chapter in step 6

6. **Verify real decryptability** (`ChapterFetcher::try_fetch_chapter`):
   - For **each candidate chapter**:
     - Fetches `chapters/chapter-<NN>.enc` from GitHub
     - Checks if your username appears in its `wrappedKeys` map
     - **Present** → marked as **readable**
     - **Absent, or 404 (not published yet)** → marked as **pending** (not hidden, but not selectable)
   - Malformed files or network errors are folded into "pending" (one bad chapter doesn't crash the whole menu)

7. **Display menu** (`ReaderMenu`):
   - Shows all chapters (both readable and pending) in ascending chapter-number order
   - Readable chapters can be selected; pending chapters are shown but grayed out / not selectable
   - Reader selects a chapter number, or exits to return to launch prompts

8. **Decrypt chapter** (`ReaderViewerService::decrypt_chapter`):
   - Retrieves your username's wrapped AES key from the chapter's `wrappedKeys` map
   - Uses your stored private key to RSA-OAEP-unwrap it
   - Uses the recovered AES key to AES-256-GCM-decrypt the ciphertext
   - **Success** → plaintext is displayed
   - **Unwrap failed** (wrong private key, or corrupted file) → clear error: "Cannot unwrap key — check with writer"
   - **Decryption failed** (tampered/corrupted ciphertext) → clear error: "Decryption failed — corrupted chapter"
   - Never returns partial/garbage output

9. **Display chapter** and return to menu (steps 7-8 repeat until reader exits)

**Data Model:**

- **Input:** Writer's GitHub username/repo; your username
- **Network:** GitHub Contents API (public repo only, no auth token)
- **Local storage:** Private key in `%APPDATA%\BuddyShare\keys\<username>_private.pem` (DPAPI-protected on Windows)
- **Fetched files:** `access.txt` (parsed by shared `AccessRegistry`); chapter `.enc` files (parsed by shared `ChapterFile`)
- **Cryptography:** RSA-OAEP (private key unwrap) + AES-256-GCM (plaintext decrypt) via shared `CryptoProvider`

**Key Integration Points:**

- **`CryptoProvider` (spec 002):** The reader uses the *same* crypto class that the writer used for encryption, guaranteeing parameter alignment (RSA-OAEP key wrapping, AES-256-GCM settings). No cross-language interop risk—it's one C++ class used identically on both paths.
- **`AccessRegistry` (spec 003):** The reader uses the same parser that the writer uses, reading from a network source instead of a local file.
- **`ChapterFile` (spec 002):** The reader reads the same `.enc` JSON format that the writer produces.
- **`GitHubClient` (spec 001):** The reader reuses the same HTTP client as the writer's bootstrap step (both use Contents API).

**Caveats:**

- **Public repo requirement:** The writer's GitHub repo must be **public**. BuddyShare makes unauthenticated Contents API calls; if the repo is private, every file fetch returns 404 indistinguishably from "doesn't exist," making the feature unusable. This is intentional: no token storage, no credential management on the reader side.

- **Private key loss:** If your local `<username>_private.pem` file is deleted or corrupted after you've already sent your public key to the writer, you'll generate a **new keypair** next run. The writer's `access.txt` will still reference your old public key—decryption will fail for every chapter until they update it. This is a known limitation (spec 004's open question), not a design flaw—the alternative would be to store keys elsewhere or support recovery, adding complexity that spec 003 explicitly deferred.

- **Chapter list pre-authorization:** You can reference chapters that don't exist yet in `access.txt` (e.g., your Apprentice list says `1,5` but chapter 5 hasn't been published). Pending chapters are shown in the menu but not selectable—no error, no confusion.

- **Wrapped key mismatch:** If `access.txt` permits you to read chapter 3, but the actual chapter-03.enc file's `wrappedKeys` doesn't include you, you'll see "pending — not yet available" in the menu. This matches spec 002/003's "no retroactive re-wrap" rule: the writer published chapter 3 before adding you, so you didn't get wrapped. Only future chapters include your key.

- **Rate limits:** GitHub API's unauthenticated rate limit (~60 requests/hour) is acceptable for personal use. Large-scale deployments would need token-based auth (future enhancement, out of scope).

### 6. Defect Fix: Reader Access Edits Now Commit & Push (Spec 003)

**Summary of the Fix:**

During spec 003's initial implementation, `ReaderAccessController::edit_existing_reader` was updating `access.txt` locally but was NOT committing and pushing those changes to the remote repository. This violated spec 003's Acceptance Criterion 3: "Editing an existing reader changes only their level/chapter-list line; name and public key are unchanged; **produces its own commit.**"

**The Impact:**

When a writer edited a reader's access level or chapter list, the local `access.txt` file was updated immediately, but readers fetching `access.txt` from the remote repository (via spec 004) would see the outdated version. The updated access level would never reach readers unless the writer manually performed a git commit and push.

**The Fix:**

- `ReaderAccessController` now receives an `IGitProcess` dependency via constructor injection (the same pattern used by `ChapterEncryptionService`)
- After a successful edit, the program automatically:
  1. Stages `access.txt` with `git add access.txt`
  2. Commits with message: `chore: update access for <name> to <level>`
  3. Pushes to the remote repository with `git push`
- If any validation fails during the edit prompt (e.g., invalid level or chapter list), **zero git operations occur** — the program simply re-prompts for valid input
- If the reader name doesn't exist in `access.txt`, **zero git operations occur** — the program returns an error message

**Verification:**

- The fix is covered by new unit tests in `tests/unit/ReaderAccessControllerTest.cpp`:
  - Test: "EditExistingReader_SuccessfulEdit_StagesCommitsAndPushesAccessTxt" verifies the git sequence
  - Test: "EditExistingReader_UnknownName_RunsNoGitCommands" verifies no git operations on invalid names
  - Test: "EditExistingReader_InvalidInputDuringPrompt_NoGitCommandsUntilValidCompletion" verifies no partial commits on re-prompted input
- Integration: `WriterMenu`'s "Manage reader access" (choice 2) now correctly wires the git process to `ReaderAccessController`

**Behavior after the fix:**

When you edit a reader from the Writer Menu, your change immediately appears in the remote `access.txt`. Readers can refresh their chapter list and see the new access level take effect on their next run (for future chapters, not retroactively).

### 7. Defect Fix: Silent Git-Failure Detection (2026-09-11)

**Summary of the Problem:**

During spec 002 and spec 003 implementations, several services were performing git operations (add, commit, push) but were not checking whether those operations actually succeeded. If a git command failed due to network errors, permission issues, or misconfiguration, the program would still print a success message to the user, masking the real failure. This meant:

1. Writers believed their chapters were encrypted **and pushed** when in fact only the local encryption succeeded
2. Writers believed their reader access changes were **committed and pushed** when only the local file was updated
3. Writers believed their git repository was **properly configured** when branch tracking setup failed

**Affected Code Paths:**

- `GitBootstrapService::run()` — `git branch --set-upstream-to=...` exit code was never checked
- `ChapterEncryptionService::encrypt_chapter()` — final push exit code was never checked
- `ChapterEncryptionService::register_reader()` — push exit code was never checked
- `ReaderAccessController::edit_existing_reader()` — push exit code was never checked

**The Fix:**

All four services now explicitly check the exit code of the final (or critical) git operation:

1. **GitBootstrapService::run():**
   - After `git push` succeeds, runs `git branch --set-upstream-to=origin/<branch> <branch>` to enable branch tracking
   - Now checks that command's exit code
   - If it fails: keeps the push success (it did succeed) but changes the message to: "Push succeeded, but branch tracking could not be confirmed. You may need to run `git push -u origin <branch>` manually before the next encrypt/edit-access action."
   - If it succeeds: prints the existing success message as before

2. **ChapterEncryptionService::encrypt_chapter():**
   - After encrypting and committing the chapter locally, runs `git push`
   - Now checks the push exit code
   - If it fails: returns `EncryptChapterOutcome::push_failed` with message: "Chapter encrypted locally but NOT pushed to GitHub. Check your git connection before assuming readers can see it."
   - If it succeeds: returns the existing success outcome

3. **ChapterEncryptionService::register_reader():**
   - After adding a reader to `access.txt` and committing, runs `git push`
   - Now checks the push exit code
   - If it fails: still returns success (the local registry update did succeed) but prints: "Reader registered locally but push failed. Check your git connection."
   - If it succeeds: proceeds with the existing silent success path

4. **ReaderAccessController::edit_existing_reader():**
   - After editing a reader's level/chapters and committing, runs `git push`
   - Now checks the push exit code
   - If it fails: still returns success (the local edit did succeed) but prints: "Reader access updated locally but push failed. Check your git connection."
   - If it succeeds: proceeds with the existing silent success path

**Testing:**

The fix extends the existing `FakeGitProcess` test doubles (used in all four test files) to support configurable exit codes for specific subcommands. New unit tests verify:

- Push failure is detected and reported (not swallowed as success)
- The distinction between local success and remote failure is clear
- Users are given actionable guidance ("check git connection," "run git push -u manually")
- Early validation failures still short-circuit before any git operations

**Impact on Users:**

Writers will now get honest feedback about whether their changes actually reached GitHub. Network issues, auth failures, and misconfiguration will surface as clear, actionable messages instead of silent failures. If a push does fail, the local files remain unchanged—the next run will retry automatically when the user repeats the action.

### 8. Enhancement: Honest GitHub Rate-Limit Handling (2026-09-11)

**Summary of the Problem:**

When readers fetch from GitHub (spec 004), they make unauthenticated HTTP requests to the Contents API. GitHub limits these to ~60 requests/hour per IP address. If that limit is hit, the server responds with HTTP 403 (Forbidden), but previously, BuddyShare would:

1. Detect the network error
2. Print a generic "Network error" message
3. Return to menu, leaving the reader confused about whether the issue is temporary, permanent, or self-inflicted

There was no distinction between "rate limit hit" and "repo doesn't exist" or "network is down."

**The Fix:**

1. **HttpGitHubClient (src/shared/HttpGitHubClient.cpp):**
   - Both `get_repo()` and `get_contents()` now explicitly check for HTTP 403 before the generic network error handler
   - When 403 is detected, sets `rate_limited = true` and `network_error = true`, and sets error_message to the friendly static text: "GitHub's request limit has been reached (0/60 remaining this hour). Wait for it to reset, or connect to a VPN for a new IP address."
   - Existing callers that just print `error_message` automatically get the new wording—no callback changes needed

2. **Data Model Updates:**
   - `RepoInfo` struct gains `bool rate_limited{false};` field
   - `ContentsResult` struct gains `bool rate_limited{false};` field
   - Callers can distinguish rate-limited errors from other network failures by checking this flag

3. **Reader-Side Integration (ChapterFetcher):**
   - `try_fetch_chapter()` now checks `rate_limited` on the underlying fetch result
   - If rate-limited: returns `ChapterFetchResult{ rate_limited = true }` immediately (before the "doesn't exist" fallback)
   - `list_master_candidates()` return type changes from bare `std::vector<int>` to a small struct: `MasterCandidatesResult { std::vector<int> candidates; bool rate_limited{false}; }`
   - This distinction prevents a rate-limited directory listing from being folded into "empty chapters folder"

4. **Main Menu Display (src/main.cpp run_reader_menu()):**
   - After calling `list_master_candidates()` or `try_fetch_chapter()`, checks the `rate_limited` flag
   - If rate-limited on the first hit: prints the friendly GitHub rate-limit message and `continue` back to the launch prompts (same error recovery pattern as other failures)
   - Subsequent readable chapters are not fetched; the menu is not shown (better to tell the user "try later" than to show a half-built menu)
   - `fetch_access_txt()` already has a network error handler that prints `error_message`, so it automatically gets the friendly text once HttpGitHubClient sets it

**Testing:**

The existing `FakeGitHubClient` (used in `ChapterFetcherTest.cpp`) is extended to return rate-limited `ContentsResult` and `RepoInfo` objects. New unit tests verify:

- 403 response correctly sets `rate_limited = true`
- The friendly rate-limit message is printed to the user
- Reader menu returns to launch prompts on first rate-limit detection
- Non-rate-limited errors still work as before

**Impact on Users:**

Readers hitting the unauthenticated rate limit will see a clear explanation and a practical workaround (use a VPN or wait). The reader menu won't hang or display confusing partial data. Writers are unaffected (bootstrap uses token auth where available, and encryption doesn't depend on GitHub API rate limits).

### 9. Enhancement: Improved Prompt Text for First-Time Users (2026-09-11)

**Summary of the Enhancement:**

The prompts for chapter encryption, reader registration, and access level management were terse and assumed familiarity with the tool. First-time users often didn't know what format to use, what a "public key" was, or what "Apprentice" meant in context. This enhancement adds **inline examples and clarification text** to key prompts—no behavior change, just friendlier wording.

**Changes:**

1. **GitBootstrapService.cpp:**
   - **Before:** "Repo name:"
   - **After:** "Repo name (must already exist on GitHub, empty, e.g. \"Buddy-Share\"):"

2. **ChapterEncryptionService.cpp:**
   - **Before:** "Chapter number:"
   - **After:** "Chapter number (e.g. 1):"
   - **Before:** "Path to the plaintext chapter file:"
   - **After:** "Path to the plaintext chapter file (e.g. chapters-source/chapter-01.txt):"
   - **Before:** "New reader's public key (base64 SPKI/DER):"
   - **After:** "New reader's public key (they get this from their own first Reader run -- paste it exactly):"

3. **ReaderAccessController.cpp:**
   - **Before:** "Access level (Master/Apprentice/Novice):"
   - **After:** "Access level -- Master sees every chapter, Apprentice sees a chosen list, Novice sees exactly one. Enter Master, Apprentice, or Novice:"
   - **Before:** "Chapter numbers for this Apprentice reader (comma-separated):"
   - **After:** "Chapter numbers for this Apprentice reader, comma-separated (e.g. 2,3):"
   - **Before:** "Chapter number for this Novice reader:"
   - **After:** "Chapter number for this Novice reader (exactly one, e.g. 1):"

4. **main.cpp (Reader launch prompts):**
   - **Added before username/repo prompts:** One new line printed: "Enter the writer's GitHub details and the username you registered (or want to register) with them:"

**Test Impact:**

Existing unit tests that assert exact prompt/print strings will need their expected strings updated to match the new wording. This is expected churn—the tests are verifying that the prompts appear, not that they're in any specific format. Updating expected strings is mechanical and low-risk.

**Impact on Users:**

First-time writers and readers will see more guidance inline with their prompts, reducing the need for external documentation and lowering the activation energy for the tool. Experienced users see slightly longer prompts but still get the job done. No UX flow or control-flow logic is changed—this is purely cosmetic.

### 10. Enhancement: Multi-Entry .gitignore Seeding (2026-09-11)

**Summary of the Enhancement:**

Previously, `seed_gitignore()` in `GitBootstrapService.cpp` seeded only a single hardcoded entry (`chapters-source/`) into the writer's `.gitignore`. This has been generalized to seed **two entries**: `chapters-source/` (unchanged, first in the list) and `*.exe` (new, to prevent compiled executables from being accidentally committed).

**The Implementation:**

1. **Generalized entry list:**
   - Replaced `constexpr std::string_view kGitignoreEntry = "chapters-source/"` with `constexpr std::array<std::string_view, 2> kGitignoreEntries = {"chapters-source/", "*.exe"}`
   - The array is small and fixed; future entries can be added the same way

2. **Per-entry duplicate detection:**
   - `seed_gitignore()` now loops over each entry in `kGitignoreEntries`
   - For each entry, checks if it already exists in the file (whether from a prior run or user hand-edit)
   - Appends only missing entries, never duplicating existing ones
   - Preserves all other file content (hand-added entries remain)

3. **Preserved behavior:**
   - Creates `.gitignore` if it doesn't exist
   - Adds a leading newline only if the existing content doesn't already end with one
   - No entries are removed or reordered

**Edge Case Handling:**

Example: User or prior run already added `*.exe` to `.gitignore`, but not `chapters-source/`:
- `*.exe` is found to exist → skipped (not duplicated)
- `chapters-source/` is found to be missing → appended
- Result: both entries present, neither duplicated

**Testing:**

New unit tests in `GitBootstrapServiceTest.cpp` verify:
1. Fresh bootstrap's `.gitignore` contains both `chapters-source/` and `*.exe`
2. Pre-existing entries are not duplicated (via multiple bootstrap runs or hand-editing)
3. Missing entries are still appended even if others already exist
4. Seeding happens before `git add`, so entries are included in the initial commit

**Rationale:**

- **`chapters-source/`** — plaintext chapter sources must never reach git (spec 002 requirement)
- **`*.exe`** — compiled executables (BuddyShare.exe, tests/buddyshare_tests.exe) are build artifacts and should not be versioned; writers should only commit source and encrypted chapter files

**Impact on Users:**

Writers' `.gitignore` is now seeded with both entries in one shot. No extra steps needed. If a writer has already run bootstrap (before this enhancement) and manually added `*.exe` themselves, re-running bootstrap (if somehow possible) will not duplicate it. Future writers get both protections by default.

### 11. CLI Output Styling (Spec 005)

**What it does:**

Adds color, text styling, and visual banners to `BuddyShare.exe`'s console output — no changes to prompts, menu flow, or business logic. Menu headers now display in boxes, success messages appear in green, errors in red, warnings in yellow, and informational status lines (like "Pushing to GitHub...") appear in cyan. The styling respects the `NO_COLOR` environment variable and automatically disables itself if output is redirected to a file or non-interactive console.

**Architecture:**

The implementation extends the shared `IConsole` interface (the abstraction every class already uses for testability):

1. **`ConsoleStyle`** (`src/shared/ConsoleStyle.h/.cpp`) — pure string-formatting helpers:
   - `should_colorize(bool is_tty, bool no_color_env_set)` — true only when writing to an interactive terminal and `NO_COLOR` is unset
   - `apply_style(text, style, colorize)` — wraps text in ANSI escape codes per `MessageStyle` (or returns text unchanged if `colorize=false`)
   - `make_banner(title)` — returns a boxed/bordered rendering: e.g. `┌─ Writer Menu ─┐` with side borders
   
2. **`IConsole::print` signature change** — adds optional `MessageStyle` parameter (defaults to `plain`):
   ```cpp
   virtual void print(const std::string& message, 
                      MessageStyle style = MessageStyle::plain) = 0;
   ```
   Every existing call site remains valid without modification; only call sites this spec categorizes pass an explicit style.

3. **`ConsoleIO` integration** — detects tty status and `NO_COLOR` at startup:
   - `stdout_is_tty()` — checks if stdout is connected to an interactive terminal
   - `no_color_env_is_set()` — checks the `NO_COLOR` environment variable
   - `enable_vt_processing()` — enables Windows virtual-terminal processing (ANSI support) on legacy `cmd.exe`; silently continues in plain mode if the OS doesn't support it
   - Before printing, applies style via `ConsoleStyle::apply_style` with the computed `colorize` flag

4. **`FakeConsole` (test double)** — updated to optionally record which `MessageStyle` each call used, so tests can verify styling without asserting on raw ANSI bytes

**Style Categories:**

| Style | Color | Use Case |
|-------|-------|----------|
| `header` | Cyan + bold | Menu banners only (Writer/Reader Menu titles) |
| `success` | Green | Terminal good outcomes (chapter encrypted, reader added, push succeeded) |
| `error` | Red | Terminal failures (git/network errors, validation failures, decryption failures) |
| `warning` | Yellow | Partial-success or noteworthy-but-not-fatal (some readers skipped, key mismatch, malformed lines tolerated) |
| `info` | Cyan | Instructional notices and pre-action status lines ("Pushing to GitHub...", "Generating keypair...") |
| `plain` | No color | Menu items, chapter plaintext content, and other non-decorated text (default) |

**Where Styles Are Applied:**

1. **Menu headers** (spec 001): "Writer Menu" and "Reader Menu" are now rendered via `make_banner` in `header` style
2. **Status lines** (before blocking calls):
   - `GitBootstrapService`: "Pushing to GitHub..." before `git push`
   - `ChapterEncryptionService`: "Encrypting chapter..." before encryption, "Wrapping keys..." before key wrapping, "Pushing to GitHub..." before push
   - `ReaderViewerService`: "Generating RSA keypair..." before key generation
   - `main.cpp` (reader path): "Fetching chapters..." before the per-chapter loop
3. **Outcomes** (after operations):
   - Success messages: "Chapter encrypted for X readers" (green)
   - Error messages: "Network error", "Unauthorized", "Cannot unwrap key" (red)
   - Warnings: "Reader public key parse failed (skipping alice)", "Some readers could not be wrapped" (yellow)
   - Informational: "Already bootstrapped", "New keypair generated, send your public key to the writer" (cyan)
4. **Menu items and chapter content**: remain `plain` (no styling)

**Environment Variables & Defaults:**

- **`NO_COLOR`**: If set to any non-empty value, color is suppressed regardless of tty status (respects the no-color.org convention)
- **`stdout` is a file/pipe**: `is_tty` returns false, so no ANSI codes are written to redirected output
- **Windows 10 legacy `cmd.exe`**: `enable_vt_processing()` attempts to enable ANSI support via `SetConsoleMode`; if it fails (e.g., Windows 7 conhost), the program silently falls back to plain mode — never crashes

**Caveats & Design Decisions:**

- **Chapter plaintext content is never styled:** When a reader views a decrypted chapter, the plaintext is always printed in `plain` style, preserving the exact content without color wrapping (reader's actual prose must not be decorated)
- **Styling is additive only:** Existing message wording, prompt text, and control flow are unchanged; styling only changes how messages are rendered, never what they say or when they're shown
- **No third-party dependencies:** All ANSI code formatting is hand-written in `ConsoleStyle`; no CLI library (rang, fmt, termcolor) is vendored
- **Tests unaffected:** Existing unit tests in specs 001-004 continue to pass without modification; `FakeConsole` still records plain message text, so assertions on output content remain valid
- **Backward compatible:** Older terminals or piped output that don't support ANSI codes simply see plain text (codes are filtered out if output is redirected)

**Testing:**

1. `ConsoleStyleTest.cpp` (new) covers:
   - `should_colorize` returns true only when both tty and no-color conditions are met
   - `apply_style` with `colorize=false` returns text unchanged for every style
   - `apply_style` with `colorize=true` wraps text in ANSI codes
   - `make_banner` produces a boxed string containing the original title

2. Integration tests verify:
   - Menu headers display via `make_banner`
   - Blocking calls print `info`-styled status lines before the operation
   - No ESC (`\x1B`) bytes appear in output when stdout is redirected to a file
   - Every existing GoogleTest suite (specs 001-004) passes without modification

**Impact on Users:**

Writers and readers get clearer visual feedback at a glance:
- Menu headers stand out visually, making navigation easier
- Success messages appear in green, errors in red, helping users spot problems immediately
- Status lines ("Pushing to GitHub...") indicate that a blocking operation is running (not hung)
- The styling works the same in modern Windows Terminal and legacy `cmd.exe`
- If output is logged to a file, no color codes pollute the log

This is purely presentational — no new features, no new prompts, no changes to the underlying workflows.

### 12. Error Handling

Each failure point returns you to the appropriate menu (program doesn't exit):

| Scenario | Behavior |
|----------|----------|
| Repository not found (404) | Clear error message; link to https://github.com/new to create it |
| Private repo + no token (ambiguous 404) | Message states: "This may also mean the repo is private and no token was given" |
| Invalid/expired token (401) | Distinct error from "not found" |
| Remote repo already has commits | Aborts before any local changes; no partially-configured state left |
| Git missing and winget fails | Manual install instructions; returns to menu |
| Network error during validation/push | Prints the error; no automatic retry |
| Repo name is a full URL or has `.git` suffix | Automatically normalized (e.g., `https://github.com/me/repo.git` → `repo`) |

**Reader-Specific Error Scenarios:**

| Scenario | Behavior |
|----------|----------|
| Writer's repo doesn't exist or is private (no token) | "Repository not found" error; returns to launch prompts |
| Network error while fetching | Clear error message; returns to launch prompts (no automatic retry) |
| Your username not in `access.txt` | Shows your public key with "Not registered — send this to the writer"; returns to launch prompts |
| Chapter file missing/not published yet | Shown as "pending — not yet available" in menu (not selectable) |
| Your wrapped key missing from chapter file | Marked "pending" (writer hasn't re-wrapped this chapter yet for you) |
| Private key file corrupted/missing after registration | Program generates new keypair; decryption fails for all chapters until writer updates `access.txt` with new public key |
| Ciphertext tampered/corrupted | "Decryption failed" message; never displays garbage plaintext |
| Private key doesn't match writer's registered public key | "Cannot unwrap key" error during decryption |

---

## Build Instructions

### Prerequisites

- **Windows 10+** with PowerShell or Git Bash
- **CMake 3.20+**
- **C++17 compiler** (MSVC, Clang, or MinGW/g++)
- **Git** (will be auto-installed by BuddyShare if missing, but helpful to have before building)

### Build Steps

```bash
# From the project root
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

The executable will be at `build\Release\BuddyShare.exe` (MSVC) or `build\BuddyShare.exe` (MinGW).

### Run Tests

```bash
cmake --build . --config Release --target RUN_TESTS
# or
ctest
```

---

## Usage

### Basic Workflow

```bash
# First time on this machine:
$ BuddyShare.exe
Are you the Writer or a Reader? [W/R]: W

--- Writer Menu ---
1) Initialize repo
0) Exit
Choose an option: 1

[Prompted for GitHub username, email, repo name, optional token]
[Validates repo exists and is empty]
[Runs git init, commit, push]

Success! Repo URL: https://github.com/{username}/{repo}
Branch: main
Local branch is now tracking origin/main

--- Writer Menu ---
0) Exit
Choose an option: 0
```

```bash
# Second time on the same machine:
$ BuddyShare.exe
[No prompt — role is already locked in]

--- Writer Menu ---
0) Exit
Choose an option: 0
```

### Reader Scenarios (Spec 004)

**Scenario R1: First-time reader setup**

```bash
$ BuddyShare.exe
[No prompt — role is already locked in]

Writer's GitHub username: alice
Repository name: my-chapters
Your username: bob

=== First time with username "bob" ===
[Generating RSA keypair...]

Your public key (send this to alice):
-----BEGIN PUBLIC KEY-----
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA...
-----END PUBLIC KEY-----

[Fetching access.txt...]
Error: You're not registered yet in alice's access.txt.
Send alice this public key so she can add you.

[Returning to launch prompts...]
Writer's GitHub username:
```

**Scenario R2: Registered reader viewing chapters**

```bash
$ BuddyShare.exe
Writer's GitHub username: alice
Repository name: my-chapters
Your username: bob

[Reusing stored keypair for bob...]
[Fetching access.txt...]
[Verifying chapter availability...]

--- Reader Menu ---
1) Chapter 1 (readable)
2) Chapter 3 (readable)
3) Chapter 5 (pending — not yet available)
0) Exit

Choose a chapter: 1

[Decrypting chapter 1...]

=== Chapter 1 ===
Once upon a time, in a land far away...
[... full plaintext content ...]

Press any key to continue...

--- Reader Menu ---
1) Chapter 1 (readable)
2) Chapter 3 (readable)
3) Chapter 5 (pending — not yet available)
0) Exit

Choose a chapter: 0

Writer's GitHub username:
```

**Scenario R3: Master reader (all chapters)**

```bash
Writer's GitHub username: alice
Repository name: my-chapters
Your username: master-alice

[Reusing stored keypair...]
[Verifying chapter availability...]

--- Reader Menu ---
1) Chapter 1 (readable)
2) Chapter 2 (readable)
3) Chapter 3 (readable)
4) Chapter 4 (readable)
5) Chapter 5 (readable)
0) Exit

[Master readers see all published chapters]
```

**Scenario R4: Apprentice reader (specific chapters only)**

```bash
Writer's GitHub username: alice
Repository name: my-chapters
Your username: apprentice-carol

[Reusing stored keypair...]
[Verifying chapter availability...]

--- Reader Menu ---
1) Chapter 2 (readable)
2) Chapter 4 (readable)
3) Chapter 7 (pending — not yet available)
0) Exit

[Apprentice reader carol is authorized for chapters 2, 4, 7
 — only 2 and 4 are currently wrapped in actual .enc files]
```

### Writer Scenarios

**Scenario 1: Already have a `.git` directory**

The "Initialize repo" menu item won't even appear. If you somehow invoke it anyway, you'll see:

```
A git repository already exists in this folder; this bootstrap only runs once.
```

Then return to the menu with no changes.

**Scenario 2: Using a personal access token**

When prompted for "Token", enter your GitHub token. It will **never** be:
- Echoed to the console
- Written to any file
- Left in `.git/config`
- Logged anywhere

It's used once for the push, then discarded.

**Scenario 3: No token (public repo, or using cached credentials)**

Leave the token prompt blank. The push will use whatever auth method your system has set up (Git Credential Manager, SSH agent passphrase in memory, etc.).

---

## Security & Caveats

### Chapter Encryption (Spec 002) ⚠️

- **Password security:** The password is only as strong as what the writer types. A weak password defeats AES-256-GCM encryption. Use a strong, unique password (not shared with readers).

- **Password not persisted:** The encryption password exists only in RAM during this single menu invocation. It's not saved in any file, config, or credential manager. Each future encryption requires re-entering the password.

- **Malformed reader keys are tolerated:** If a reader's stored public key in `access.txt` fails to parse as valid RSA-SPKI/DER, that reader is silently skipped during encryption with a console warning. The chapter file is still written and pushed; that reader simply won't have a wrapped key entry. This is intentional: a single bad key shouldn't abort the entire encryption run.

- **No retroactive re-wrapping:** If you encrypt chapter 1, then later add a new reader to `access.txt`, chapter 1's `.enc` file is **not** automatically updated with that reader's wrapped key. Only chapters encrypted *after* adding the reader will include them. To "add" the new reader to chapter 1, you'd need to re-encrypt it.

- **Zero-reader chapters are allowed:** If `access.txt` is empty after the explicit warning prompt, you can proceed with encryption. The result is a valid `.enc` file with empty `wrappedKeys` map — valid JSON, but undecryptable until you add and re-register readers.

- **Salt and IV are always random:** A new random salt is generated per encryption (enabling the same password to produce different ciphertexts for different chapters). Both salt and IV are stored in the output file and are not secret.

- **GCM authentication tag prevents tampering:** If any single byte of the ciphertext is altered post-encryption, decryption fails with "authentication tag mismatch" — never returns garbage plaintext. This applies on the reader side (spec 004).

### Reader Access Levels (Spec 003) ⚠️

- **Chapter numbers are not validated at registration time:** A writer can assign a reader to chapter 99 even if only 3 chapters have been written. This is intentional — writers often pre-authorize access for future chapters. Invalid chapter references don't cause errors; they simply won't match during encryption.

- **Editing commits and pushes automatically:** When you edit a reader's access level and chapter list, the program automatically stages, commits, and pushes `access.txt` to the remote repository. This ensures readers fetching `access.txt` (via spec 004) see the change immediately. If git operations fail (e.g., network error during push), a clear error message is shown, and you return to the Writer Menu—the local `access.txt` file remains updated.

- **Editing access doesn't retroactively re-encrypt:** If a reader's access is narrowed (e.g., Apprentice list changes from `1,2,3` to `1,2`), chapters already encrypted with their wrapped key **remain unchanged**. Only chapters encrypted **after** the access change reflect the new restriction. This matches spec 002's precedent: changes take effect going forward.

- **Malformed lines in `access.txt` are skipped with a warning:** If the file is hand-edited incorrectly (e.g., wrong field count, invalid level string, empty chapter list for Apprentice), that reader is silently skipped during the next encryption run, and a console warning is printed. The rest of the file still loads. The program doesn't abort on a single malformed line.

- **Editing a reader that doesn't exist returns an error:** If you try to edit a reader name that's not in `access.txt`, the program prints "No reader named \"{name}\" is registered yet" and returns to the menu. No git operations are performed (nothing to commit). Editing is a modification operation, not an implicit registration.

- **Invalid input during edit doesn't trigger git operations:** If you mis-enter a level or chapter list during the edit prompt, re-prompted answers don't cause partial commits. Git operations (add/commit/push) only occur once, after the entire edit sequence completes successfully with valid input.

### Token Handling ⚠️

- **Never displayed on screen** — prompt is masked
- **Never persisted** — exists only in RAM during the push
- **Never logged** — not present in error messages, even if git tries to echo it
- **Verified absent** — after success, check `git remote -v` and `.git/config`; the origin URL is always `https://github.com/{username}/{repo}.git` (token-free)

### Reader's Private Key Storage (Spec 004) ⚠️

- **Location:** `%APPDATA%\BuddyShare\keys\<username>_private.pem` (one file per username)
- **Protection:** Windows DPAPI (Data Protection API) encrypts the file at rest using your Windows login credentials
- **Never transmitted:** The private key never leaves your machine, never sent to the writer, never logged
- **First-time display:** Your public key (derived from the private key) is displayed on screen with send-to-writer instructions; the private key itself is never shown
- **Loss recovery:** If the file is deleted after registration, you'll generate a *new* keypair. The writer's `access.txt` still references your old public key—decryption fails until they update it. This gap is the same one flagged in spec 003 (key rotation/recovery remains unresolved; inherits from design decision).
- **Scope:** Protected by Windows login credentials; if someone gains access to your Windows account, they can potentially access your private key. This is acceptable for personal use (single-machine, trusted environment).

### Role Lock is Permanent

Once you choose Writer or Reader on your machine, that role is locked in:
- There is no menu option to switch roles
- The program is structurally designed to make role-switching impossible (no such command exists)
- Manually deleting `%APPDATA%\BuddyShare\.role` will unlock it, but this is **unsupported and undocumented**—it's a reset path for emergencies, not a feature

### `.gitignore` is Automatically Created with Multiple Entries

When you run the git bootstrap:
- A `.gitignore` file is created (or updated) with two entries: `chapters-source/` and `*.exe`
- `chapters-source/` is **required** by spec 002; do not remove it (plaintext sources must never be committed)
- `*.exe` prevents compiled executables from being accidentally committed
- If you already have a `.gitignore`, new entries are appended while existing entries remain untouched
- If an entry already exists in the file (e.g., the user or a prior bootstrap run added it), that entry is not duplicated

### Repository Must Already Exist and Be Empty

- BuddyShare will **not** create a GitHub repo for you
- The repo must already exist at `https://github.com/{username}/{repo}`
- The repo must be empty (no commits)
- Create it first at https://github.com/new, then run BuddyShare

### Git Installation on Windows

If git is not found:
1. BuddyShare automatically attempts: `winget install --id Git.Git`
2. If winget is unavailable or fails, manual instructions are printed
3. You can install git from https://git-scm.com/download/win if needed

### Network Dependency

The GitHub API validation step requires network access:
- If your network is down or GitHub is unreachable, you'll get a clear error
- The program returns to the menu (no retry loop, no hanging)

### No Credential Persistence

Beyond the `.role` file (which is not secret):
- No credentials are stored in files
- No entries added to Windows Credential Manager
- No environment variables set
- Each future push (spec 002) will either use cached system credentials or fail with an auth error

---

## Complete Feature Set

**All four core specs have been implemented:**

- **Spec 001:** Role selection (`RoleManager`) and one-time git bootstrap (`GitBootstrapService`)
  - Writers set up their GitHub repo once; readers pick a repo each run (not one-time)

- **Spec 002:** Chapter encryption (`ChapterEncryptionService`) — encrypt plaintext chapters with password + RSA-wrapped keys per reader
  - Writer supplies password (RAM-only, never persisted)
  - Ciphertext is committed/pushed; plaintext stays in gitignored `chapters-source/`

- **Spec 003:** Reader access management (`ReaderAccessController`) — assign access levels (Master/Apprentice/Novice) and chapter permissions
  - Master: all chapters, past and future
  - Apprentice: explicit chapter list
  - Novice: single chapter (previews)
  - Changes apply to new encryptions only (no retroactive re-wrap)

- **Spec 004:** Reader-side chapter viewer (`ChapterFetcher`, `ReaderViewerService`) — fetch encrypted chapters from GitHub and decrypt using reader's own RSA private key
  - RSA keypair generated per username, stored locally (DPAPI-protected)
  - Fetches `access.txt` and chapter `.enc` files from public GitHub repos (no token, no git required)
  - Menu shows readable and pending chapters; readers decrypt independently without knowing writer's password

- **Spec 005:** CLI output styling (`ConsoleStyle`, extended `IConsole`) — color, banners, and status lines
  - Menu headers display in bordered boxes (cyan + bold)
  - Success messages in green; errors in red; warnings in yellow; status lines in cyan
  - Respects `NO_COLOR` environment variable and auto-disables for redirected output
  - No changes to prompts, menu flow, or business logic — purely presentational

**Complete Workflow:**
1. Writer runs bootstrap once (spec 001)
2. Writer encrypts chapters and registers readers (spec 002)
3. Writer manages reader access levels (spec 003)
4. Readers view their authorized chapters (spec 004)
5. Both roles see styled, color-coded output with clear status indicators (spec 005)

Both Writer and Reader roles are fully operational in a single executable with professional console presentation.

---

## Troubleshooting

### Role Selection & Git Bootstrap

| Issue | Solution |
|-------|----------|
| "Unrecognized option" in role prompt | Type exactly `W` or `R` (case-insensitive). Re-prompts on anything else. |
| "Repository not found" | Check the username/repo name. Create the repo at https://github.com/new first. |
| "This may also mean the repo is private and no token was given" | If your repo is private, provide a valid GitHub Personal Access Token (needs `repo` scope). |
| "Invalid or expired token" (401) | Generate a new token at https://github.com/settings/tokens/new (scope: `repo`). |
| "A git repository already exists in this folder" | You've already bootstrapped this folder. Use an empty folder or delete `.git` to reset (advanced). |
| Token still visible in git config | This shouldn't happen. Run `git remote -v` and `cat .git/config` to verify; contact the maintainers if it is. |
| Git command not found | Install git from https://git-scm.com/download/win, or let BuddyShare auto-install via `winget`. |

### Chapter Encryption (Spec 002)

| Issue | Solution |
|-------|----------|
| "A git repository already exists in this folder, but no origin remote is configured" | Run spec 001's "Initialize repo" menu item first. Chapter encryption requires git bootstrap to be complete. |
| "No readers are registered yet. Add one? (y/n)" | Respond `y` to register your first reader (provide their public key). Alternatively, `n` to proceed with a zero-reader chapter (ciphertext only, not decryptable until you add readers later). |
| "Blank password entered" | Passwords cannot be empty. Re-prompted automatically; enter a non-blank password. |
| "Error: reader public key parse failed (skipping <name>)" | That reader's stored public key in `access.txt` is malformed. Edit `access.txt` to fix or remove it; or regenerate the reader's keypair and re-register. Encryption continues for other readers; this reader is simply missing from the wrapped keys. |
| "Access.txt is missing or empty" | Create readers via the "Add a reader? (y/n)" prompt during encryption, or manually add entries to `access.txt` (format: `name:base64-pubkey:Level:chapters`). |
| ".enc file created but wrapped keys are empty" | Either no readers were registered, or none were authorized for that chapter number. Check `access.txt` and reader access levels. |

### Reader Access Levels (Spec 003)

| Issue | Solution |
|-------|----------|
| "Unknown access level" when editing a reader | Type exactly `Master`, `Apprentice`, or `Novice` (case-insensitive). Re-prompts on anything else. |
| "Chapter list cannot be empty for Apprentice" | Apprentice readers must have at least one chapter. Provide a comma-separated list like `1,2,3`. |
| "Novice readers require exactly one chapter" | Novice readers must have exactly one chapter number, not a list. |
| "Non-numeric chapter number" | Chapter numbers must be integers (e.g., `1,2,3`), not names or decimal numbers. |
| "Reader not found in access.txt" | The reader name you're editing doesn't exist. Editing is a modification, not an implicit registration. Register the reader first via spec 002. |
| Malformed line skipped during encryption | Check `access.txt` manually; hand-edited lines must follow the 4-field format exactly. |

### Reader Chapter Viewing (Spec 004)

| Issue | Solution |
|-------|----------|
| "Repository not found" or "Network error" | Check the writer's username and repo name. Verify the repo exists at https://github.com/{username}/{repo} and is **public**. |
| "Private repo" error | BuddyShare does not support private repos on the reader side. Ask the writer to make the repo public, or the feature cannot work. |
| "You're not registered yet — send this public key to the writer" | Copy the displayed public key and send it to the writer. They will add you to `access.txt` in their next encryption. Come back later. |
| All chapters showing "pending" even after writer says they encrypted | Check that the writer has registered your username in `access.txt`. Even if chapters are published, if your public key isn't there, they won't have your wrapped key. Ask writer to verify. |
| Chapter shows "readable" but "Cannot unwrap key" error when selected | Your stored private key doesn't match the public key the writer registered. This can happen if your key file was lost and regenerated. Ask the writer to update `access.txt` with your new public key. |
| "Decryption failed — corrupted chapter" when opening a chapter | The chapter file is tampered, corrupted, or was encrypted with different crypto settings. Ask the writer to re-encrypt it. If the problem persists, contact the maintainers. |
| First-time run shows "public key" but I'm already registered | The program checks for a stored private key. If the file is missing/corrupted, a new keypair is generated. Send your new public key to the writer. |
| My public key keeps changing / different every run | Your private key file at `%APPDATA%\BuddyShare\keys\<username>_private.pem` may be missing or corrupted. Check that the file exists and is not empty. If lost, ask the writer to re-register you with your new public key. |
| "Invalid or corrupted username" | Usernames cannot be empty. Re-prompted automatically. |

---

## Architecture Notes

### Modular Monolith Design

- **One executable**, multiple responsibilities
- Each responsibility lives in its own class (e.g., `RoleManager`, `GitBootstrapService`)
- No circular dependencies; clear data flow: Role → Menu → Bootstrap → Exit
- Later specs add new responsibilities (encryption, access control) in the same pattern

### Testing Strategy

- `RoleManager` is testable via the `IRoleStore` abstraction (no real filesystem touches)
- `GitBootstrapService` is testable via `IGitHubClient` and `IGitProcess` abstractions
- `ConsoleIO` captures user input/output for verification
- Full integration tests exist in `tests/` (see CMakeLists.txt for build setup)

### Why No Env Vars or Config Files?

- Keeps the install footprint small (one hidden file only)
- Reduces the surface for leaking credentials
- Simpler mental model: role once, bootstrap once, then use the features

---

## Additional Resources

- **GitHub Token Setup:** https://github.com/settings/tokens/new (generate token with `repo` scope)
- **Create a New Repo:** https://github.com/new
- **Git Credential Manager:** Built-in to modern git for Windows; handles cached credentials
- **Project Source:** the project root (specs, source code, tests)
