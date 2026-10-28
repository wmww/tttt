// @strict: true
// @target: es2022
// The first statement of an ambient block is TS1036 (tsc's checkGrammarStatementInAmbientContext), which ends a for
// statement's grammar checks: no TS1123 for its empty declaration list, no TS1103 for its `await` (bun 28-C26c,
// 28-C26e); a later one gets them
declare namespace N { for (var;;) {} for (let;;) {} }
declare namespace M { for await (const x of []) {} for await (const y of []) {} }
for (const;;) {}
