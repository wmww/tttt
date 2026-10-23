// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const t: [string, number];
const { [/*c*/ 2]: t2 } = t;
const sentinel: number = "";
