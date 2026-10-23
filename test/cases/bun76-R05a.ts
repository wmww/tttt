// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface A { a: number }
interface B { b: number }
interface X5<T> extends A, B { [k: string]: X5<T>["a"] }
declare const x5: X5<string>;
const y5: number = x5.zzz;
function f5<U>(x: X5<U>) { const r: number = x.zzz; }
interface Y5 extends X5<string> { more: 1 }
declare const yy5: Y5;
const z5: number = yy5.zzz;
// in tsc only the instantiation that the index signature names loses it
interface X8<T> extends A, B { [k: string]: X8<string>["a"] }
declare const x8: X8<string>;
const y8: number = x8.zzz;
declare const x8n: X8<number>;
const y8n: number = x8n.zzz;
// an interface with `this`: the type itself loses it, an interface derived from it does not
interface X9 extends A, B { self: this; [k: string]: X9["a"] | this }
declare const x9: X9;
const y9: number | X9 = x9.zzz;
interface Y9 extends X9 { more: 1 }
declare const yy9: Y9;
const z9: number | Y9 = yy9.zzz;
// one base: the two agree
interface X7<T> extends A { [k: string]: X7<T>["a"] }
declare const x7: X7<string>;
const y7: number = x7.zzz;
const sentinel: number = "";
