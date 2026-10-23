// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const p: { foo(): void };
if (p.foo) { let x: typeof p.foo; }
const sentinel: number = "";
