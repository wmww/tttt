// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
class A {
  inst = 1;
  static stat = 1;
  m1() { interface J extends inst {} }
  m2() { class B implements stat {} }
  m3() { class B implements inst.X {} }
}
const sentinel: number = "";
