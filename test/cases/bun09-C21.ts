// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare let o: { p: { a: string | number } };
let d6; (({ a: d6 }) = o.p); const l: never = d6;
const sentinel: number = "";
