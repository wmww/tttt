// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a_umd.d.ts
export const v: number; export as namespace UX; export as namespace UC;
// @filename: b_glob.d.ts
declare namespace UX { const extra: number; } declare namespace UC { const other: number; }
// @filename: c.ts
UX.extra; UC.extra;
const sentinel: number = "";
