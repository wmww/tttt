// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const f1: (a: number, b: string, c: boolean) => void = (x, ...y, z) => {};
const sentinel: number = "";
