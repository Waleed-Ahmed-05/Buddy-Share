export const meta = {
  name: 'life-build',
  description: 'TDD pipeline for L.I.F.E: write tests, implement + simplify, self-test and independently re-test in BIN/Black_Box, then review + docs',
  phases: [
    { title: 'Write Tests', detail: 'tdd-guide writes failing tests before any implementation exists' },
    { title: 'Implement', detail: 'general-purpose coder implements, then code-simplifier passes over it' },
    { title: 'Self-Test', detail: 'coder builds and runs the tests fresh inside BIN/Black_Box' },
    { title: 'Independent Test', detail: 'tdd-guide independently re-verifies in BIN/Black_Box, blind to the coder\'s context' },
    { title: 'Review & Docs', detail: 'code-reviewer and doc-updater run in parallel; doc-updater writes BS_Doc.md' },
  ],
}

// Usage: Workflow({ name: 'life-build', args: { spec: 'implement the writer-side PowerShell push script' } })
const spec = args?.spec
if (!spec) {
  throw new Error('life-build requires args.spec: a description of the feature to implement')
}

const MAX_ATTEMPTS = args?.maxAttempts ?? 3

const TEST_RESULT_SCHEMA = {
  type: 'object',
  properties: {
    passed: { type: 'boolean' },
    summary: { type: 'string' },
    failures: { type: 'string' },
  },
  required: ['passed', 'summary'],
}

log(`life-build: ${spec}`)

phase('Write Tests')
const testPlan = await agent(
  `You are the testing agent for the L.I.F.E chapter-sharing tool (C++ CLI reader + PowerShell writer tooling, GitHub-backed).
Feature to implement: ${spec}

Write GoogleTest tests FIRST, before any implementation exists, following this project's cpp-testing skill conventions. The tests are expected to fail right now since nothing is implemented yet.
Place test files in the normal source tree test location for this project — do not create or touch anything under BIN/, that is reserved for build/run scratch space only.
This project has no npm/package.json/Node.js anywhere — it's C++ (CMake/CTest/GoogleTest) and PowerShell only. Ignore any npm-test or JS/web-framework instructions from your own default role description; they don't apply here.
Report back which test files you wrote and a one-paragraph summary of what they cover, including edge cases.`,
  { agentType: 'tdd-guide', label: 'tdd-guide: write tests' }
)

let implementationSummary = null
let selfTest = null
let independentTest = null
let attempt = 0

while (attempt < MAX_ATTEMPTS) {
  attempt++
  phase('Implement')
  log(`Implementation attempt ${attempt}/${MAX_ATTEMPTS}`)

  const failureContext = selfTest && !selfTest.passed
    ? `\n\nThe previous attempt failed self-testing. Failure details:\n${selfTest.failures}`
    : (independentTest && !independentTest.passed
        ? `\n\nThe previous attempt failed independent testing. Failure details:\n${independentTest.failures}`
        : '')

  implementationSummary = await agent(
    `You are the coding agent for the L.I.F.E chapter-sharing tool.
Feature to implement: ${spec}

Tests already exist, written by the testing agent: ${testPlan}

Implement just enough code to make these tests pass, following this project's cpp-coding-standards skill. You may create the top-level source folder(s) this spec's design calls for (e.g. writer-side/, reader-side/) if they don't exist yet — that's normal feature work. Just don't invent any extra scratch/build folders outside BIN/ for anything else — that constraint is absolute.${failureContext}
Report back a summary of what you changed and why.`,
    { agentType: 'general-purpose', label: `coder attempt ${attempt}` }
  )

  await agent(
    `Apply the code-simplifier agent's standard: simplify the files just changed for this feature (${spec}) for maximum readability — a 12-year-old should be able to follow the logic — WITHOUT changing behavior.
Changes summary from the coder: ${implementationSummary}
Do not touch test files. Report what you simplified.`,
    { agentType: 'code-simplifier', label: 'code-simplifier pass' }
  )

  phase('Self-Test')
  selfTest = await agent(
    `You are the coding agent, self-verifying your own work for: ${spec}
Delete BIN/Black_Box/ entirely if it exists (do not reuse it), then recreate it — it is the only folder you may create outside the existing source tree. Do a full CMake reconfigure and build from that clean directory: no reused CMakeCache.txt, no reused object files, no reused build artifacts from any prior step in this pipeline (including your own implementation step, if you built anywhere earlier). This must be a true clean-room build, not an incremental one, even if that costs more time. Then run the tests against that build.
If the build fails, apply cpp-build-resolver conventions to fix build errors with minimal changes, then rebuild (still from a clean BIN/Black_Box/ — wipe it again before the rebuild).
In your summary, state the exact command you used to delete/recreate BIN/Black_Box/ before configuring, so this can be audited.
Report structured pass/fail with failure details if any.`,
    { agentType: 'cpp-build-resolver', label: `self-test attempt ${attempt}`, schema: TEST_RESULT_SCHEMA }
  )

  if (!selfTest?.passed) {
    log(`Self-test failed on attempt ${attempt}: ${selfTest?.summary ?? 'unknown failure'}`)
    continue
  }

  phase('Independent Test')
  independentTest = await agent(
    `You are an INDEPENDENT tester for the L.I.F.E project, re-verifying work you did not write, for: ${spec}
Delete BIN/Black_Box/ entirely first — do not reuse the coder's or self-tester's existing build artifacts, CMakeCache.txt, or object files under any circumstance — then recreate it and do a full CMake reconfigure and build from that clean directory. Re-run the existing tests against that build, then add and run at least one edge case the coder may have missed.
In your summary, state the exact command you used to delete/recreate BIN/Black_Box/ before configuring, so this can be audited.
This project is C++ (CMake/CTest/GoogleTest) and PowerShell only — no npm/Node.js. Use ctest, not npm test.
Report structured pass/fail with failure details if any.`,
    { agentType: 'tdd-guide', label: `independent test attempt ${attempt}`, schema: TEST_RESULT_SCHEMA }
  )

  if (independentTest?.passed) break
  log(`Independent test failed on attempt ${attempt}: ${independentTest?.summary ?? 'unknown failure'}`)
}

if (!selfTest?.passed || !independentTest?.passed) {
  log(`Stopped after ${attempt} attempt(s) without a full pass — not proceeding to review.`)
  return {
    status: 'failed',
    attempts: attempt,
    spec,
    testPlan,
    selfTest,
    independentTest,
  }
}

phase('Review & Docs')
const [review, docs] = await parallel([
  () => agent(
    `Review the diff implementing: ${spec}
for the L.I.F.E chapter-sharing tool. This project handles GitHub tokens and per-reader encryption keys — flag any credential-handling, key-management, or access-control issues in addition to normal code quality.
This project is C++ (CMake/CTest/GoogleTest) and PowerShell only — no npm/Node.js/React/SQL/Supabase. Ignore any checklist items from your own default role description that assume those stacks; they don't apply here.`,
    { agentType: 'code-reviewer', label: 'code-reviewer' }
  ),
  () => agent(
    `Write or update BS_Doc.md at the project root documenting the feature just implemented: ${spec}
Summarize what it does, how to use it, and any caveats surfaced during implementation, self-testing, or independent testing. Match this project's existing documentation style if BS_Doc.md already exists.
Write directly to BS_Doc.md, not docs/CODEMAPS/* — that's this project's actual documentation convention regardless of your own default role description.`,
    { agentType: 'doc-updater', label: 'doc-updater: BS_Doc.md' }
  ),
])

return {
  status: 'passed',
  attempts: attempt,
  spec,
  testPlan,
  implementationSummary,
  selfTest,
  independentTest,
  review,
  docs,
}
