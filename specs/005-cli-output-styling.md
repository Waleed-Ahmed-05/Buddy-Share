# 005 — CLI Output Styling

## Summary
Add color, banners, and status-line indication to `BuddyShare.exe`'s existing
console output — no change to the prompt/menu flow, control structure, or wording of
any message.

## Motivation / Problem
The console app is currently 100% undecorated `std::cout`/`std::cin`: no color, no
visual distinction between a menu header, a success message, and an error, and no
indication that a blocking git/network operation is in progress versus hung. A writer
running `Encrypt a chapter` or `Initialize repo` sees identical-looking plain text
whether the push just succeeded, failed, or is still running. This spec makes the
existing output easier to scan at a glance without touching any business logic.

## Architecture Context
This spec extends the shared `IConsole` seam every other class already depends on for
testability — it does not add a new component to the role dispatch in
`RoleManager`/`main.cpp`. It touches:
- **`IConsole`** (`src/shared/Console.h`) — the interface both `WriterMenu`'s and
  `ReaderMenu`'s call sites, and every writer/reader service, already call through.
- **`ConsoleIO`** (`src/shared/ConsoleIO.{h,cpp}`) — the one real implementation
  (`std::cout`/`std::cin` + `<conio.h>` for masked input), spec 001. This is the only
  place that touches an actual OS console/terminal.
- **New: `ConsoleStyle`** (`src/shared/ConsoleStyle.{h,cpp}`) — pure string-formatting
  helpers with no OS dependency, so the formatting logic itself is unit-testable
  without a real console.
- **`FakeConsole`** (`tests/support/Fakes.h`) — the shared test double used by every
  existing GoogleTest suite (specs 001-004); must keep recording plain message text
  unchanged so none of that existing coverage breaks.
- Every existing call site across `src/main.cpp`, `src/writer/GitBootstrapService.cpp`,
  `src/writer/ChapterEncryptionService.cpp`, `src/writer/ReaderAccessController.cpp`,
  `src/core/RoleManager.cpp`, `src/reader/ReaderViewerService.cpp`,
  `src/shared/AccessRegistry.cpp` — categorized into a style, per the rule in Behavior
  below, not rewritten.

## User Stories
- As a writer, I want the menu headers to stand out visually from the options beneath
  them, so I can tell at a glance which menu I'm in.
- As a writer or reader, I want success messages, errors, and warnings to look
  different from each other and from plain informational text, so I don't have to
  read every word carefully to tell whether something failed.
- As a writer, I want to see that a git push or chapter encryption is actually running
  (not just a frozen prompt) before its result prints.
- As a writer or reader, I want this to work the same whether I'm in a modern Windows
  Terminal or a legacy `cmd.exe` window, and I never want to see raw escape-code
  garbage if I redirect the program's output to a file.

## Scope
**In scope:**
- A `MessageStyle` enum (`plain`, `header`, `success`, `warning`, `error`, `info`) added
  as a defaulted parameter to `IConsole::print`.
- ANSI color rendering in `ConsoleIO`, gated on: stdout being a real interactive
  console, and the `NO_COLOR` environment variable being unset/empty.
- A simple banner/box helper (`ConsoleStyle::make_banner`) replacing the plain
  `"--- Writer Menu ---"` / `"--- Reader Menu ---"` header lines
  (`src/main.cpp:75,199`).
- Static "before" status lines (styled `info`) immediately preceding each blocking
  git/network call, pairing with that call's existing "after" result message (now
  styled `success`/`error`/`warning`): the git init/commit/push sequence in
  `GitBootstrapService.cpp` (~line 179), the two pushes in
  `ChapterEncryptionService.cpp` (~lines 109 and 152), and one line preceding the
  reader's per-chapter fetch loop in `main.cpp` (~line 181) — one line for the whole
  loop, not one per candidate chapter.
- Categorizing every existing `print()` call site across the seven files listed in
  Architecture Context into a `MessageStyle`, per the rule below.
- `FakeConsole` updates so tests can (optionally) assert which style a call site used.

**Out of scope:**
- Any change to message *wording*, prompt text, control flow, menu numbering, or what
  happens on each menu choice — this spec only changes how existing/new lines are
  *rendered*, never what they say or when they're shown (except the net-new "before"
  status lines, which are new lines, not rewordings).
- A full TUI (arrow-key navigation, boxed panels) or non-interactive flag/subcommand
  mode — both considered and explicitly rejected in favor of this lighter approach.
- An animated spinner. The blocking calls this spec adds status lines around are
  synchronous; animating a spinner would require a background thread writing to the
  same stdout while the call blocks — real threading/race complexity for a cosmetic
  gain. A static "before" line plus the existing "after" result is judged sufficient.
- Any third-party CLI/color library (rang, fmt, termcolor, etc.). The project has zero
  third-party CLI dependencies today; the ANSI rendering needed here is small enough
  (a handful of fixed styles) to hand-write in `ConsoleIO`/`ConsoleStyle` rather than
  vendor and pin an external header for it.
- `prompt()`/`prompt_masked()` styling — unchanged; only `print()` gains a style.

## Behavior / Functional Requirements
1. **`IConsole::print` signature change** (`src/shared/Console.h`):
   `virtual void print(const std::string& message, MessageStyle style = MessageStyle::plain) = 0;`
   The defaulted parameter means every existing call site remains valid unchanged;
   only call sites this spec deliberately recategorizes pass a non-default style.
2. **`ConsoleStyle` helpers** (new `src/shared/ConsoleStyle.h/.cpp`), pure functions
   with no OS/console dependency:
   - `bool should_colorize(bool is_tty, bool no_color_env_set)` — true only when
     `is_tty` and `!no_color_env_set`.
   - `std::string apply_style(const std::string& text, MessageStyle style, bool colorize)`
     — returns `text` unchanged when `colorize` is false; otherwise wraps it in the
     ANSI code for that style (e.g. cyan/bold for `header`, green for `success`, red
     for `error`, yellow for `warning`/`info`) followed by a reset code.
   - `std::string make_banner(const std::string& title)` — returns a boxed/bordered
     rendering of `title` (plain text; styling is applied separately via `apply_style`
     with `MessageStyle::header` when printed).
3. **`ConsoleIO::print`** (`src/shared/ConsoleIO.cpp`) computes `colorize` once via:
   - `stdout_is_tty()` — `_isatty(_fileno(stdout))`. This is a **separate** check from
     `prompt_masked`'s existing `_isatty(_fileno(stdin))` (different fd, different
     purpose) — the two must not be merged or share one flag.
   - `no_color_env_is_set()` — true if the `NO_COLOR` environment variable exists and
     is non-empty (per the no-color.org convention).
   - `enable_vt_processing()` — calls `SetConsoleMode` with
     `ENABLE_VIRTUAL_TERMINAL_PROCESSING` once at startup; checks the return value and
     silently continues in plain mode if it fails (legacy `cmd.exe`/conhost that
     rejects the flag must never crash or error).
   Then calls `ConsoleStyle::apply_style(message, style, colorize)` before writing to
   `std::cout`.
4. **Menu banners:** `src/main.cpp:75` and `:199` use
   `console.print(ConsoleStyle::make_banner("Writer Menu"), MessageStyle::header)` (and
   the Reader equivalent) in place of the current plain `"--- Writer Menu ---"` /
   `"--- Reader Menu ---"` strings.
5. **Status lines around blocking calls:** immediately before each blocking git/network
   call listed in Scope, print a new `MessageStyle::info` line describing what's about
   to happen (e.g. "Pushing to GitHub..."); the call's existing follow-up message is
   restyled `success` on success, `error`/`warning` on failure per the rule below —
   no change to what that follow-up message says.
6. **Call-site categorization rule** — style reflects what the print *represents*, not
   which class calls it:
   - `header`: the two menu banners only.
   - `success`: terminal good outcomes, e.g. `GitBootstrapService.cpp:219`
     (push-succeeded message), `ChapterEncryptionService.cpp:132` (chapter-encrypted
     confirmation).
   - `error`: terminal failures, e.g. `GitBootstrapService.cpp:86/100/106/114/205`
     (git-unavailable/network-error/unauthorized/repo-not-found/push-failed),
     `ChapterEncryptionService.cpp:118` (push-failed), `main.cpp:144/212/223/226/231`
     (repo-not-found, unselectable chapter, decryption-failure variants).
   - `warning`: partial-success or noteworthy-but-not-fatal, e.g.
     `ChapterEncryptionService.cpp:156` (registered locally but push failed), `:165`
     (zero wrapped keys), `AccessRegistry.cpp:131` (malformed line skipped), `:186`
     (reader already registered, not overwriting).
   - `info`: instructional/pre-action notices, e.g. `GitBootstrapService.cpp:157`
     (already-bootstrapped notice), `main.cpp:130` (reader launch instructions),
     `ReaderViewerService.cpp:31` (new-keypair message), plus every new "before" status
     line from requirement 5.
   - `plain` (unchanged, do not restyle): numbered menu items and `"0) Exit"` lines,
     and — critically — **`main.cpp:220`, the decrypted chapter's own plaintext
     content** — chapter text a reader is reading must never be wrapped in banner or
     color styling.
7. **`FakeConsole`** (`tests/support/Fakes.h`) — `print(message, style = MessageStyle::plain)`
   continues to record `message` into its existing `printed_`/`all_output()` exactly as
   today, and additionally records `style` into a new vector exposed via a
   `style_calls()` (or similar) accessor, so new tests can assert which style a given
   call site chose without asserting on literal ANSI bytes anywhere.

## Data Model / API / CLI Surface
- **No new executable, network surface, or file format.** This is a presentation-only
  change to the existing `BuddyShare.exe` (spec 001) console output.
- **New public API surface:** `MessageStyle` enum (`src/shared/Console.h`);
  `ConsoleStyle::should_colorize`, `ConsoleStyle::apply_style`, `ConsoleStyle::make_banner`
  (new `src/shared/ConsoleStyle.h`).
- **No token/key/sensitive-data handling** — this spec touches no encryption, no
  GitHub token, no private-key code path. Flagged for `security-reviewer` mainly for
  completeness (confirm no accidental logging change), not because the change itself
  is security-relevant.
- **Environment:** reads the `NO_COLOR` environment variable (read-only, no new
  environment variables written or required).

## Edge Cases & Error Handling
- **Legacy `cmd.exe`/conhost without ANSI VT support:** `SetConsoleMode` may fail to
  enable `ENABLE_VIRTUAL_TERMINAL_PROCESSING`; the program must check the return value
  and fall back to uncolored plain output, never error or crash over it.
- **Piped/redirected stdout** (e.g. `BuddyShare.exe > log.txt`, or stdout captured by
  a test harness): `stdout_is_tty()` must be false in this case, so no raw ANSI escape
  bytes are ever written to a file or non-terminal consumer.
- **`NO_COLOR` set:** color must be suppressed regardless of tty status, per the
  no-color.org convention.
- **Color-blind accessibility:** styling must pair with wording that's already
  distinguishing ("failed", "error", success confirmations, etc.) — color is a visual
  accent, never the only signal of outcome.
- **Chapter plaintext content** must never be styled/boxed (see requirement 6's
  `plain` category) — a chapter's actual prose is user content, not a status message.

## Acceptance Criteria
1. `ConsoleStyle::should_colorize(true, false)` returns `true`; `(false, false)`,
   `(true, true)`, and `(false, true)` all return `false`.
2. `ConsoleStyle::apply_style(text, style, /*colorize=*/false)` returns `text`
   unchanged, for every `MessageStyle` value.
3. `ConsoleStyle::apply_style(text, style, /*colorize=*/true)` returns a string that
   still contains `text` as a substring (wrapped, not replaced), for every
   `MessageStyle` value.
4. All existing GoogleTest suites (specs 001-004) continue to pass unmodified against
   the new `IConsole::print` signature (the defaulted `style` parameter requires no
   changes to any existing test's assertions).
5. A new `tests/unit/ConsoleStyleTest.cpp` covers criteria 1-3 above directly, with no
   real console/tty required.
6. Redirecting `BuddyShare.exe`'s stdout to a file produces output containing no ESC
   (`\x1B`) byte.
7. The menu header lines render via `make_banner` + `MessageStyle::header` instead of
   the old plain `"--- Writer Menu ---"` / `"--- Reader Menu ---"` strings.
8. A blocking git push (`GitBootstrapService`, `ChapterEncryptionService`) prints an
   `info`-styled line before the call and a `success`/`error`/`warning`-styled line
   after, using the same wording as today's existing follow-up message.

## Open Questions
- Exact ANSI color choices per `MessageStyle` (e.g. specific 8-color vs. 256-color
  codes) are left to implementation-time judgment within the color-blind-safe
  guidance above — not locked in here since they don't affect testable behavior.
- Whether `warning` and `info` should render as visually distinct colors or share one
  (both are "non-fatal, pay attention") is left to implementation; either satisfies
  this spec's acceptance criteria.
