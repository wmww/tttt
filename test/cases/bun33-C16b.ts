// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] }, "files": ["a.ts", "b.ts", "c.ts", "t.ts"] }
// @filename: a.ts
export {};
declare module "./t" { interface A {} }
// @filename: b.ts
export {};
declare module "./t" { interface I {} export = I; }
// @filename: c.ts
export {};
declare module "./t" { interface C {} }
const sentinel: number = "";
// @filename: t.ts
export {};
