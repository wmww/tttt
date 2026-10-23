// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare let t1: [...a?: string[]];
const n1: never = t1;
const sentinel: number = "";
