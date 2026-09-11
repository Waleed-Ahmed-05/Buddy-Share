# CLAUDE.md

Behavioral guidelines to reduce common LLM coding mistakes. Merge with project-specific instructions as needed.

**Tradeoff:** These guidelines bias toward caution over speed. For trivial tasks, use judgment.

## 1. Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

Before implementing:
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them - don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

## 2. Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

## 3. Surgical Changes

**Touch only what you must. Clean up only your own mess.**

When editing existing code:
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it - don't delete it.

When your changes create orphans:
- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: Every changed line should trace directly to the user's request.

## 4. Goal-Driven Execution

**Define success criteria. Loop until verified.**

Transform tasks into verifiable goals:
- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:
```
1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]
```

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.

---

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation rather than after mistakes.

---

## Project-Specific Tooling (L.I.F.E Chapter Sharing Tool)

This repo vendors a shortlist of specialist agents/skills from the ECC
plugin (`.claude/agents/`, `.claude/skills/`), pinned locally so they
work even without the ECC plugin installed elsewhere. Reach for them
as follows:

**Agents:**
- `cpp-reviewer` — review the C++ reader-side executable (ownership,
  safety, idioms) after writing or changing it.
- `cpp-build-resolver` — fix C++/CMake build errors incrementally.
- `security-reviewer` — review anything touching tokens, the
  per-reader encryption keys, or the GitHub API calls; use proactively
  before considering that code done.
- `tdd-guide` — the "testing agent" in the `life-build` workflow below
  (writes tests first, and independently re-verifies later).
- `code-simplifier` — simplifies implementation code for readability
  (target: a 12-year-old should be able to follow it) without changing
  behavior.
- `code-reviewer` — final review pass before a feature is considered
  done.
- `doc-updater` — writes/updates `BS_Doc.md`.

**Skills:**
- `cpp-coding-standards` — C++ conventions reference for this project.
- `cpp-testing` — TDD workflow (GoogleTest) for the C++ side.
- `security-review` — structured security review workflow; run this
  over the key-handling and access-control logic specifically.
- `security-scan` — scans for leaked secrets/config issues.
- `github-ops` — GitHub repo/release operations reference for the
  writer-side scripts.
- `git-workflow` — git workflow conventions for commits/branches.

These were copied verbatim from `~/.claude/plugins/marketplaces/ecc/`
and are frozen snapshots — they won't pick up upstream ECC updates
automatically. Re-copy manually if a newer version is needed.

## Build Pipeline: `life-build` Workflow

`.claude/workflows/life-build.js` is a saved, reusable Workflow script
implementing the project's TDD pipeline end to end:

1. `tdd-guide` writes failing tests first, for the feature described.
2. A general-purpose coding agent implements just enough to pass them,
   following `cpp-coding-standards`.
3. `code-simplifier` passes over the new code for readability.
4. The coder builds and runs the tests fresh inside `BIN/Black_Box/`
   (self-test), using `cpp-build-resolver` if the build fails.
5. `tdd-guide` independently rebuilds and re-tests the same
   `BIN/Black_Box/` output, blind to the coder's context, adding at
   least one edge case of its own.
6. On failure at either test stage, the pipeline retries from
   implementation with the failure details attached, up to 3 attempts
   total, then stops and reports rather than looping forever.
7. On a full pass, `code-reviewer` and `doc-updater` run in parallel —
   `doc-updater` writes/updates `BS_Doc.md` at the project root.

**Folder rule:** this restricts the Self-Test and Independent Test stages only —
they may only create new folders inside `BIN/` (specifically `BIN/Black_Box/`,
the isolated build+run directory both stages share) and never scaffold new
source-tree folders themselves. The Implement stage is different: it may create
whatever top-level source folder(s) a spec actually names (e.g. `writer-side/`,
`reader-side/`) if they don't exist yet — that's normal feature work, not scratch
space, and is expected the first time a feature touching a new folder is built.

**Invoke it** with a feature description:
```
Workflow({ name: "life-build", args: { spec: "implement the writer-side PowerShell push script" } })
```
Optional `args.maxAttempts` overrides the default retry cap of 3.

## Feature Spec Authoring Process

Before any feature is implemented, it gets a detailed PRD-style spec first.
Features are supplied one at a time — write the spec, let the user review/revise
it, and only then move to the next feature. Don't draft multiple specs ahead of
what's been asked for.

- **Location:** `specs/NNN-feature-slug.md` (zero-padded sequence number +
  kebab-case slug), e.g. `specs/001-writer-push-script.md`.
- **Depth:** each spec should cover:
  1. Title & one-line summary
  2. Motivation / problem it solves
  3. User stories ("as a writer/reader, I want to...")
  4. Scope — explicit in-scope and out-of-scope bullets
  5. Behavior / functional requirements, step by step
  6. Data model / API / CLI surface (flag anything touching tokens or the
     per-reader encryption keys for `security-reviewer` / `security-review`)
  7. Edge cases & error handling
  8. Acceptance criteria — testable statements that can feed directly into
     `tdd-guide`'s test-first step in `life-build`
  9. Open questions — flag anything undecided rather than guessing
- Ask clarifying questions where genuinely ambiguous, per "Think Before Coding"
  above, instead of guessing at requirements.
- Specs are living documents: if a later feature changes an earlier spec's
  design, flag the conflict and update the earlier spec rather than diverging
  silently.
- No implementation code is written during spec authoring — that's a separate,
  later step (via `life-build` or otherwise).
