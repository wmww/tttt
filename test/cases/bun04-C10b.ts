// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noUnusedParameters": true } }
// @filename: a.ts
const sentinel: number = "";
// @filename: g1.ts
type GA<T> = 1;
// @filename: g2.ts
declare const GA: number;
