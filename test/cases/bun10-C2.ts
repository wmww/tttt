// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const k: string;
const x1 = { [k]: 1, get g() { return x1; } };
const sentinel: number = "";
