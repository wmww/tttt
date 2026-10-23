// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function base<T>(x: T): new () => { v: T };
// checkConditionalExpression tests the condition for TS2774 / TS2801 at once: it asks for the call signatures of its type.
declare const x1: C1;
const k1 = x1 ? "a" : "b";
class C1 extends base(k1) {}
// nested in the condition, nullable, in an arrow function
declare const x2: C2 | undefined, y2: boolean;
const k2 = (y2 && x2) ? "a" : "b";
class C2 extends base(k2) {}
declare const x3: C3;
const k3 = (() => x3 ? "a" : "b")();
class C3 extends base(k3) {}
// an interface
declare const x4: I4;
const k4 = x4 ? "a" : "b";
interface I4 extends Array<typeof k4> {}
// a late-bound name
declare const x5: I5;
const k5 = x5 ? "a" : "a";
interface I5 { [k5]: number }
// `&&`, `||`, `!`: the two agree
declare const x6: C6;
const k6 = x6 && "a";
class C6 extends base(k6) {}
const sentinel: number = "";
