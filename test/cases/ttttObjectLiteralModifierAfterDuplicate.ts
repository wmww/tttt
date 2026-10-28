// @strict: true
// @target: es2022
// @noEmit: true
// An object literal member's modifiers (TS1042) are tsc's checkGrammarObjectLiteralExpression's, in member order: it
// returns at a TS1118/TS1119, so no later member's modifier is reported (bun 28-C24a); a destructuring assignment
// target isn't checked as an object literal at all
declare let o: any;
let y: any, x: any;
const a1 = { get a() { return 1 }, get a() { return 1 }, public b: 1 };
const a2 = { public b: 1, get a() { return 1 }, get a() { return 1 } };
({ public x } = o);
({ public x: y } = o);
const a3 = { async x: 1 };
const a4 = { a: 1, b: 2, a: 3, public c: 1 };
const a5 = { a: 1, get a() { return 1 }, static c: 1, async d() {} };
for ({ readonly x } of [o]) {}
