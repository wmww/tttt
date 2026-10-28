// After decorators, `import` and `export` start a declaration whatever follows (tsc's parseDeclarationWorker):
// `@dec import("./m")` is an import with no clause and an expression for a module specifier (TS1141: a parenthesized
// string is no string literal), `@dec import.meta` misses that expression, `@dec export;` its `{`. TS1206 ends
// checkGrammarModifiers (no TS1191 for `export @dec import`); in a function TS1232 comes first, on the `@`
// @filename: m.ts
export {};
// @filename: a.ts
declare function dec(...x: any[]): any;
@dec import("./m");
export @dec import("./m");
function f() { @dec import("./m"); }
// @filename: b.ts
declare const dec: any;
@dec export;
// @filename: c.ts
declare function dec2(...x: any[]): any;
@dec2 import.meta;
