# 001 — App Bootstrap: Role Selection & Git Repo Initializer

## Summary
The first-run behavior of the single `BuddyShare.exe` executable: a one-time Writer/Reader
role selection that locks in permanently, followed by (for Writers only) a one-time git
bootstrap that initializes a local repo in the current folder, links it to an existing
GitHub repo, and pushes the initial commit.

## Motivation / Problem
`BuddyShare.exe` is one combined executable used by both writers and readers (see
Architecture below). Before anything else can happen, the program needs to know which role
this machine/install is for, permanently — a writer's machine should never accidentally
fall into reader prompts or vice versa. Once that's settled, writers additionally need
their project folder turned into a git repo connected to their own GitHub remote before
any push-based workflow can function. Doing this by hand (7+ commands, identity config,
token handling, default-branch naming) is repetitive and easy to get wrong, especially
around not leaking a personal access token. This feature automates both the one-time role
lock and the one-time git bootstrap safely.

## Architecture Context
This spec is part of a single combined C++ executable, `BuddyShare.exe` (one CMake project,
one binary — not separate writer/reader tools). Internally it's a modular monolith: distinct
single-responsibility classes, each owning one concern, all linked into the same executable.
This spec owns two of those classes:
- **`RoleManager`** (`src/core/RoleManager.{h,cpp}`) — first-run role prompt, persisted role
  lock, and dispatch to the Writer or Reader app loop on every launch.
- **`GitBootstrapService`** (`src/writer/GitBootstrapService.{h,cpp}`) — the git bootstrap
  behavior (unchanged from the original design of this spec), now reached as a `WriterMenu`
  item instead of being the entire program.

Other specs' classes (`ChapterEncryptionService`, `ReaderAccessController`,
`ChapterFetcher`, `ReaderViewerService`, and the shared `AccessRegistry`/`ChapterFile`/
`CryptoProvider`/`GitHubClient` classes) are defined in specs 002-004; this spec only
establishes the project skeleton, `RoleManager`, and `GitBootstrapService`.

## User Stories
- As a first-time user of `BuddyShare.exe`, I want to be asked once whether I'm the Writer
  or a Reader, and have that choice remembered forever after, so I never have to answer it
  again and can't accidentally switch roles.
- As a writer setting this up for the first time, I want one program that installs git if
  needed, asks for my GitHub details, and gets my folder pushed to my repo.
- As a writer who already ran the bootstrap once, I want the program to refuse to run it
  again on the same folder rather than silently touching my existing repo.
- As a security-conscious user, I want my token to never be shown on screen, written to
  disk, or left sitting in my git config after the push.

## Scope
**In scope:**
- `RoleManager`: first-ever-launch prompt ("Writer or Reader?"), persisting the choice to a
  hidden local state file, and dispatching every launch (this one and all future ones)
  straight to the chosen role's menu loop with no prompt and no in-app way to switch.
- The root CMake project skeleton (`CMakeLists.txt`, `src/`, `tests/` layout) that every
  other spec's classes are added into.
- Detect/install git via winget if missing (Writer role only).
- One-time interactive prompts: GitHub username, email, repo name, optional token (never
  persisted to disk).
- Validate `{username}/{repo}` exists on GitHub, and is empty, before doing anything.
- Run: init → set local identity → add → commit → add remote → push.
- Final success/failure confirmation.
- Skip the bootstrap step (no-op, not a crash) if `.git` already exists in the folder.

**Out of scope:**
- Auto-creating the GitHub repo — must already exist, or the script stops.
- Handling a remote that already has commits/history — script detects this and
  aborts rather than merge/rebase/force-push.
- Persisting credentials across runs (no config file, no credential-manager entry).
- Any ongoing/subsequent push workflow — that's spec 002's `ChapterEncryptionService`.
- SSH-based auth — HTTPS + optional token only.
- Any way to change a machine's locked-in role after the first launch (see Role Lock below)
  — deleting the hidden state file by hand is an unsupported, undocumented reset path, not
  a feature this spec builds or tests for.

> **Note for writers planning to use the Reader role (spec 004) on other machines:** that
> role has no token support and can only fetch from a **public** repo. If a private repo +
> token is chosen here, readers will not be able to fetch anything from it.

## Behavior / Functional Requirements

### 0. Role selection (`RoleManager`) — runs before anything else, every launch
1. On launch, `RoleManager::resolveRole()` checks for the hidden role-lock file at
   `%APPDATA%\BuddyShare\.role`.
   - **File missing or unparseable** → first-ever launch (or a manually-reset one): prompt
     "Are you the Writer or a Reader? [W/R]", re-prompt on any input other than W/R
     (case-insensitive), write the chosen role to `%APPDATA%\BuddyShare\.role`, set the
     Windows **hidden** file attribute on it, then continue into that role's flow for this
     same run.
   - **File present and parses to a valid role** → skip the prompt entirely, dispatch
     straight into that role's menu loop (`WriterMenu` or `ReaderMenu`).
2. Neither `WriterMenu` nor `ReaderMenu` ever expose a command or menu item to switch roles.
   This is enforced structurally (no such path is wired into either menu's dispatch table),
   not by a runtime check.
3. Reader-role behavior is defined entirely in spec 004; this spec only owns the role-lock
   mechanism and dispatch. Everything below (steps 1-7) only executes for the Writer role.

### 1-7. Git bootstrap (`GitBootstrapService`, Writer role only)
Steps 1-3 below run automatically and unconditionally the first time a Writer reaches the
"Initialize repo" step — no prompt or user confirmation is needed to trigger these checks
themselves; they always happen first, before any interactive input is requested.

1. **Startup check:** `GitBootstrapService::isAlreadyBootstrapped()` checks whether `.git`
   exists in the current directory.
   - If it already exists: the "Initialize repo" item is hidden from `WriterMenu` entirely
     (the writer goes straight to the encrypt/manage-access menu items from spec 002/003).
     If somehow invoked anyway (e.g. stale menu state), print "A git repository already
     exists in this folder; this bootstrap only runs once." and return to the menu — zero
     prompts, zero git commands. This preserves the original all-or-nothing behavior; only
     the "exit the whole program" part changes, since the whole program is no longer just
     this one feature.
   - Otherwise continue.
2. **Git availability check:** `git --version` (or equivalent process check). If missing,
   run `winget install --id Git.Git -e --source winget`, then re-check. If still missing,
   print manual-install instructions and abort this menu action (return to `WriterMenu`,
   do not exit the program).
3. **Seed `.gitignore`:** ensure a `.gitignore` file exists in the current folder and
   contains a `chapters-source/` entry — create the file with that single line if it
   doesn't exist, or append the line if the file exists but lacks it. Runs automatically,
   before `git add .` (step 6e below), so spec 002's plaintext working folder is never
   accidentally staged. This is required, not optional — spec 002 depends on
   `chapters-source/` actually being gitignored.
4. **Prompt for input**, in order, nothing persisted to disk:
   a. GitHub username
   b. Email
   c. Repo name — normalize by stripping a trailing `.git` or a full URL if the user pastes
      one instead of a bare name
   d. Token — optional, masked input; blank = skip
5. **Validate repo existence + emptiness:** `GET https://api.github.com/repos/{username}/{repo}`
   via the shared `GitHubClient` (include `Authorization: token {token}` header if given).
   - 200 → capture the `default_branch` field from the response (do not hardcode
     `"main"` — use whatever the actual repo's default branch is), then check whether
     the repo has any commits. If it already has commits → abort with a message that
     the remote isn't empty and this action won't overwrite existing history.
   - 404 → repo not found → print an error pointing to `https://github.com/new` to
     create it, and note explicitly: this may also mean the repo is private and no
     token was given (indistinguishable from "doesn't exist" without a valid token).
     Return to `WriterMenu` either way.
   - 401 (bad/expired token) → distinct error message from "not found". Return to menu.
   - Network/other errors → print the specific error, return to menu.
6. **Run git bootstrap commands, in order** (using the `default_branch` captured above,
   call it `{branch}`), via `GitProcess::run(args...)`:
   a. `git init`
   b. `git branch -M {branch}`
   c. `git config --local user.name "{username}"`
   d. `git config --local user.email "{email}"`
   e. `git add .`
   f. `git commit -m "Initial commit"`
   g. `git remote add origin https://github.com/{username}/{repo}.git` (token-free —
      this is what ends up stored in `.git/config`)
   h. Push:
      - With token: `git push https://{token}@github.com/{username}/{repo}.git HEAD:{branch}`
        (token used only as a one-off push argument, never stored), then
        `git branch --set-upstream-to=origin/{branch} {branch}` so future plain
        `git push`/`git status` work normally against `origin`.
      - Without token: `git push -u origin {branch}` (relies on the system's existing
        Git Credential Manager / cached credentials / interactive auth).
7. **Confirm outcome:** check the push command's exit code.
   - Success → print confirmation: repo URL, branch name, that local `{branch}` now
     tracks `origin/{branch}`. Return to `WriterMenu`, where "Initialize repo" is now
     hidden and the chapter/access-management items are available.
   - Failure → print git's error output as-is, return to `WriterMenu`, no automatic retry.

## Data Model / API / CLI Surface
- **Executable:** `BuddyShare.exe`, C++17+, CMake-built, single target for the whole
  project (writer + reader + this bootstrap all in one binary).
- **Role-lock file:** `%APPDATA%\BuddyShare\.role` — plain text containing `writer` or
  `reader`, created with the Windows hidden attribute set. Not encrypted (it's not secret
  data, just a local install-scoped choice) — hidden only to discourage casual/accidental
  tampering, not to withstand deliberate attack. No config file, env var, or
  credential-manager entry is created beyond this.
- **No git-related config file, env vars, or credential-manager entries are created** —
  nothing persisted across runs beyond the `.role` file and whatever `git` itself writes to
  `.git/config`.
- **External calls:**
  - GitHub REST API: `GET /repos/{owner}/{repo}` (existence + default branch + emptiness
    check), via the shared `GitHubClient` class (also used by spec 004's Contents API
    calls).
  - `winget install --id Git.Git -e --source winget` (conditional, Writer role only).
- **Sensitive value handling — flag for `security-reviewer` / `security-review` skill:**
  - Token read via masked console input; decrypted only transiently in memory to build the
    one-off push URL; never echoed to console; never written to any file; never present in
    the stored `origin` remote / `.git/config`.
  - No logging of the token under any circumstance, including error output — if git ever
    echoes a URL containing the token back on failure, it must be redacted before being
    printed.

## Edge Cases & Error Handling
- Role prompt given garbage input (not W/R) → re-prompt, don't default to either role.
- `.role` file exists but contains something other than `writer`/`reader` (corrupted) →
  treated the same as missing: re-prompt and rewrite it.
- `.git` already exists → bootstrap step no-ops (see step 1), zero prompts, zero git
  commands, program keeps running (returns to `WriterMenu`).
- Git missing and winget also fails/unavailable → clear manual-install error, return to
  menu.
- Repo doesn't exist under given username → clear error + creation link, return to menu,
  no auto-create.
- Repo private + no token → 404 is ambiguous; error message must say so explicitly.
- Token invalid/expired → distinct 401 error, not conflated with "not found".
- Remote repo already has commits → abort before any push, no force-push, no auto-merge, no
  partially-configured local repo left in a state that could later force-push by accident.
- Username/email left blank → re-prompt rather than silently committing with an empty
  identity.
- Repo name pasted as a full URL or with `.git` suffix → normalize before use.
- Network failure during validation or push → clear error, return to menu, no retry loop.
- Folder contains other unrelated files → `git add .` stages everything present in the
  folder except whatever `.gitignore` excludes (including the `chapters-source/` entry this
  step seeds); this is expected (the whole folder minus gitignored paths is what's
  committed), not a bug.
- `.gitignore` already exists but lacks a `chapters-source/` entry → append the line
  rather than overwriting the file; don't touch any other existing entries.

## Acceptance Criteria
1. First-ever launch of `BuddyShare.exe` on a machine with no `.role` file prompts for
   Writer/Reader, persists the choice as a hidden file, and does not re-prompt on the next
   launch.
2. Neither `WriterMenu` nor `ReaderMenu` contains any command that changes the locked-in
   role.
3. Folder with no `.git`, valid existing empty repo, valid or no token → results in:
   local repo, a `.gitignore` containing `chapters-source/`, default branch matching
   the remote's actual default branch, one commit ("Initial commit") with all
   non-gitignored folder contents, `origin` remote pointing at the token-free HTTPS
   URL, local branch tracking `origin/{branch}`, printed success, and `WriterMenu` no
   longer showing "Initialize repo".
4. Folder that already has `.git` → the "Initialize repo" item is hidden from
   `WriterMenu`; if triggered anyway, prints the "already initialized" message, performs
   zero git operations and zero prompts, and returns to the menu (program does not exit).
5. Username/repo combination that doesn't exist on GitHub → clear "not found — create it
   first" error, no git operations performed, returns to menu.
6. Existing but non-empty remote repo → aborts before pushing; no local git state left that
   could later force-push accidentally.
7. Token never appears in: console output, any file under the project directory,
   `.git/config`, or persists after the action completes — verify via `git remote -v` and
   inspecting `.git/config` after a token-based run (origin URL must be token-free).
8. No token, public repo with push access via system credential manager → succeeds;
   otherwise fails with a clear auth error — either way, no crash/stack trace.
9. `.gitignore` contains a `chapters-source/` entry before the initial commit runs, whether
   or not a `.gitignore` already existed in the folder beforehand — verified by checking the
   committed tree never includes files that were under `chapters-source/` at commit time.

## Open Questions
- Winget package ID assumed as `Git.Git` — confirm still current at implementation time.
- Manually deleting `%APPDATA%\BuddyShare\.role` to reset the role lock is treated as an
  unsupported, undocumented reset path per the user's decision — not something this spec
  needs to actively detect or block, but also not something to accidentally make easier
  (e.g. no "reset role" hint should ever be printed by the program).
