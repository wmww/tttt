// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const t1: { a: never } & ThisType<{ x: 1 }> = { a: { m() { this.x; } } };
const sentinel: number = "";
