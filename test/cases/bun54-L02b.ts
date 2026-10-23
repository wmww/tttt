// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare module "./t2" { const v: number; }
export {};
const sentinel: number = "";
// @filename: c.ts
declare module "./t2" { type v3 = number; }
export {};
// @filename: t2.d.ts
declare const enum E { A } export = E;
