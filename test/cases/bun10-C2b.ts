// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const k: string;
const x10 = { [k]: 1, set s(v: typeof x10) {} };
const sentinel: number = "";
