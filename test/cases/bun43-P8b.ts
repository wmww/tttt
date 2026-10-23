// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export * as #a from "./m";
export const sentinel: number = "";
// @filename: m.ts
export const a = 1;
