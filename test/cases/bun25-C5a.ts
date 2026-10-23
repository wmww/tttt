// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const a = { m() { return this.z; }, f() { return this.z; }, g: () => 1 + {} };
const sentinel: number = "";
