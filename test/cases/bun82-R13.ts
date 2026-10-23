// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function mk1() { return [new A1()]; }
function mk2() { return [new A2()]; }
class A1 { bs = [new B1()] } class B1 { cs = [new C1()] } class C1 { ds = [new D1()] } class D1 { es = [new E1()] } class E1 { x = 1 }
class A2 { bs = [new B2()] } class B2 { cs = [new C2()] } class C2 { ds = [new D2()] } class D2 { es = [new E2()] } class E2 { x = "" }
let a1 = mk1(); let a2 = mk2();
a2 = a1;
const c: C2[] = new B1().cs;
const d: D2[] = new C1().ds;
const sentinel: number = "";
