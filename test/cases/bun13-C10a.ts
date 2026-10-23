// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare let x1: [a: string] | [a: string];
const n1: never = x1;
const sentinel: number = "";
