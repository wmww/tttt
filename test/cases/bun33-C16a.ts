// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
declare module "./t" { interface I {} export = I; }
const sentinel: number = "";
// @filename: t.ts
export {};
