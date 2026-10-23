// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const obj = { meth<T>(x: T) { class M { a!: T } return new M(); } };
const p7: 1 = obj.meth(1);
const sentinel: number = "";
