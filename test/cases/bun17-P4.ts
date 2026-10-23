// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "exactOptionalPropertyTypes": true } }
// @filename: a.ts
declare const o: { a?: number | undefined };
const t: { a?: string } = { a: "x", ...o };
const sentinel: number = "";
