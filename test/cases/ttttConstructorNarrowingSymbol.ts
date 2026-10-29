// `x.constructor === C` keeps the members tsc's isConstructedBy compares by symbol when either side is a class: `typeof C`
// has the class's symbol, so it stays. bun 20-C15
// @strict: true
class A { a = 1 } class C { c = 3; static s = 1 }
namespace C { export const n = 1; }
class G<T> { g!: T }
function o2(x: typeof C | A) { if (x.constructor === C) { const _: null = x; } }
function o3(x: A | C | G<number>) { if (x.constructor === C) { const _: null = x; } else { const _2: null = x; } }
function o4(x: typeof A | typeof C) { if (x.constructor === A) { const _: null = x; } }
function o5(x: G<string> | A) { if (x.constructor === G) { const _: null = x; } }
function o6(x: { c: number } | A) { if (x.constructor === C) { const _: null = x; } }
