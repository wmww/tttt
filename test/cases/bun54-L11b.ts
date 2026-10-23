// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class C2 { get a(): number { return 1 } set a(v: string | number) {} a = 2; }
new C2().a = "s";
declare const t1: { get a(): number; set a(v: string | number); a: number };
t1.a = "s";
const p1: boolean = t1;
interface I2 { get a(): number; set a(v: string | number); a: number }
declare const i2: I2;
i2.a = "s";
class C1 { a = 2; get a(): number { return 1 } set a(v: string | number) {} }
new C1().a = "s";
const sentinel: number = "";
