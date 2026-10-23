// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
// As in ts-pattern: the typo makes `matcher` an `any`, so `[matcher]` is a name that cannot be bound.
const matcher = Symbol.forX("m");
interface Matcher { [matcher](): { match: (value: string) => boolean } }
declare function chainable<P extends Matcher>(p: P): P;
chainable({ [matcher]: () => ({ match: (value) => true }) });
chainable({ [matcher]() { return { match: (value) => true } } });

// The same without any mistake.
declare const key: string;
declare function handlers<P extends Record<string, (value: string) => unknown>>(p: P): P;
handlers({ [key]: (value) => value.nope });
handlers({ [`on${key}`]: (value) => value.nope });
// A context sensitive member with an ordinary name beside it hides the difference.
handlers({ [key]: (value) => value.nope, a: (value) => value.nope });
const sentinel: number = "";
// @filename: b.ts
// Neighbours: EVERY member propagates its flags in `checkObjectLiteral`, also one that leaves no property in the type.
export {};
declare const key: string;
declare const num: number;
declare const sym: symbol;
declare const obj: {};
type Fn = (value: string) => unknown;
declare function f<P extends Record<string | symbol, Fn>>(p: P): P;
f({ [num]: (value) => value.nope });
f({ [sym]: (value) => value.nope });
f({ [key](value) { return value.nope } });
f({ ...{}, [key]: (value) => value.nope });
declare function nested<P extends { a: Record<string, Fn> }>(p: P): P;
nested({ a: { [key]: (value) => value.nope } });
declare function list<P extends Record<string, Fn>>(...p: P[]): P;
list({ [key]: (value) => value.nope });
// What is inferred: nothing in the first pass, so `P` comes from the return type.
declare function g<P>(p: P, q: (p: P) => void): P;
const r1: number = g({ [key]: (value: any) => true, b: () => 1 }, (p) => {});
const r2: number = g({ [key]: (value) => true }, (p) => {});
const r3: number = g({ a: (value) => true, a: 1 }, (p) => {});
const r4: number = g({ [obj]: (value) => true, b: 1 }, (p) => {});
const sentinel: number = "";
