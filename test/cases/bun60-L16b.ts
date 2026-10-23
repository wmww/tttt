// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type A = (x: unknown) => asserts x is string;
const inferred = (x: unknown): asserts x is string => {};
const obj = { a: inferred, m(x: unknown): asserts x is string {} };
declare const v: unknown;
interface I { a: A; m(x: unknown): asserts x is string }
type Id<T> = { [K in keyof T]: T[K] };
declare const x1: Id<{ a: A } & typeof obj>; x1.a(v);
declare const x2: Id<typeof obj & { a: A }>; x2.a(v);
declare const x3: Id<{ a: A } & { a: A }>; x3.a(v);
declare const x4: Id<I & { z: 1 }>; x4.a(v); x4.m(v);
declare const x5: Id<I & I>; x5.a(v);
declare const x6: Id<{ a: A } | { a: A }>; x6.a(v);
declare const x7: Id<Id<I>>; x7.a(v); x7.m(v);
declare const x8: Id<Id<typeof obj>>; x8.a(v); x8.m(v);
declare const x9: Id<{ m(x: unknown): asserts x is string } & { m: A }>; x9.m(v);
declare const y1: Id<{ m: A } & { m(x: unknown): asserts x is string }>; y1.m(v);
declare const y2: Id<{ a: A } & { a: (x: unknown) => asserts x is string }>; y2.a(v);
declare const y3: { a: A } & { a: (x: unknown) => asserts x is string }; y3.a(v);
const sentinel: number = "";
// @filename: bun.related.txt
a.ts(8,46) TS2775 Assertions require every name in the call target to be declared with an explicit type anno
    related a.ts(3,15) 'a' needs an explicit type annotation.
a.ts(12,44) TS2775 Assertions require every name in the call target to be declared with an explicit type anno
a.ts(14,39) TS2775 Assertions require every name in the call target to be declared with an explicit type anno
    related a.ts(3,15) 'a' needs an explicit type annotation.
a.ts(18,74) TS2775 Assertions require every name in the call target to be declared with an explicit type anno
a.ts(19,7) TS2322 Type 'string' is not assignable to type 'number'.
// @filename: tsc.related.txt
a.ts(7,46) TS2775 Assertions require every name in the call target to be declared with an explicit type anno
a.ts(8,46) TS2775 Assertions require every name in the call target to be declared with an explicit type anno
a.ts(9,44) TS2775 Assertions require every name in the call target to be declared with an explicit type anno
a.ts(12,44) TS2775 Assertions require every name in the call target to be declared with an explicit type anno
a.ts(14,39) TS2775 Assertions require every name in the call target to be declared with an explicit type anno
    related a.ts(3,15) 'a' needs an explicit type annotation.
a.ts(15,74) TS2775 Assertions require every name in the call target to be declared with an explicit type anno
a.ts(16,74) TS2775 Assertions require every name in the call target to be declared with an explicit type anno
a.ts(17,78) TS2775 Assertions require every name in the call target to be declared with an explicit type anno
a.ts(18,74) TS2775 Assertions require every name in the call target to be declared with an explicit type anno
a.ts(19,7) TS2322 Type 'string' is not assignable to type 'number'.
