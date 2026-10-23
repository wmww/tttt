// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es5"], "target": "esnext", "declaration": true } }
// @filename: a.ts
// @ts-nocheck
export function* g() { yield 1; }
// @filename: b.ts
export {};
const sentinel: number = "";
