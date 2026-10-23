// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const e: { b?: () => boolean };
if (e.b()) { e; }
const sentinel: number = "";
