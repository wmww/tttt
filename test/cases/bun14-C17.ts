// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const o2: { m: Map<string, number> };
o2[`m`]["a"];
const sentinel: number = "";
