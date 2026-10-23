// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "exactOptionalPropertyTypes": true } }
// @filename: a.ts
export {};
declare const eo: { x?: number };
[eo.x = undefined] = [1];
const sentinel: number = "";
