// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class C {
  a = 1;
  m(): this extends { a: number } ? string : number { return null as any; }
  t() { const s: string = this.m(); }
}
const sentinel: number = "";
