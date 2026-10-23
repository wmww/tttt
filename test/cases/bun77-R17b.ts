// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class C { constructor(public a: number, public a: string) {} }
new C(1, ""); new C(1, 2);
class D { constructor(a: number, a: string) {} }
new D(1, ""); new D(1, 2);
const sentinel: number = "";
