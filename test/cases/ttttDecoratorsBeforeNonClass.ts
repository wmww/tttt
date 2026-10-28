// Decorators before something other than a class: tsc's parseDeclarationWorker dispatches on the keyword after the
// decorators and modifiers without the lookahead a statement takes, and says TS1146 "Declaration expected" where the
// declaration is missing (after the last decorator or modifier); a class member's is a property missing its name
// @filename: a.ts
declare const dec: any;
class C { @dec; }
class D { @dec }
// @filename: b.ts
@dec public x;
// @filename: c.ts
@dec interface
I {}
@dec interface = 1
// @filename: d.ts
@dec namespace class {}
// @filename: e.ts
@dec type
T = 1
// @filename: f.ts
@dec in class E {}
// @filename: g.ts
type A = 1;
export @dec type { A }
// @filename: h.ts
@dec
await 1
