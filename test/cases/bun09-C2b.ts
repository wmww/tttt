// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const u7: [a: 1] & [a: 1];
const n7: never = u7;
const sentinel: number = "";
