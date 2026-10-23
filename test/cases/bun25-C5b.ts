// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const b = { m() { return this.z; }, f() { return this.z; }, g() { return nope; } };
const sentinel: number = "";
