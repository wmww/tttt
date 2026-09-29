// @strict: true
// @noUncheckedIndexedAccess: true
// A literal in an argument whose rest parameter is a union of tuples keeps its contextual literal type, also when
// indexed access adds undefined
type Mode = 'default' | 'planetscale';
declare function f(...p: [{ a: 1 }] | [{ a: 1 }, { mode: Mode }]): void;
f({ a: 1 }, { mode: 'default' });
