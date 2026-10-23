// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface I9 extends Array<typeof a9> {}
declare const i9: I9;
const a9 = i9.length;
const sentinel: number = "";
