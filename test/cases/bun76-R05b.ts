// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface A { a: number }
interface B { b: number }
interface X2 extends A, B { own: "late"; [k2]: number }
declare const k2: X2["own"];
declare const x2: X2;
const y2: number = x2.late;
const o2: X2 = { a: 1, b: 2, own: "late", late: 3 };
// one base: the two agree, `late` is a member
interface X3 extends A { own: "late"; [k3]: number }
declare const k3: X3["own"];
declare const x3: X3;
const y3: number = x3.late;
// a class merged with the interface
class C4 { own!: "late"; [k4]!: number }
interface C4 extends A, B {}
declare const k4: C4["own"];
declare const x4: C4;
const y4: number = x4.late;
const sentinel: number = "";
