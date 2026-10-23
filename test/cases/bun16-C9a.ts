// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function foo(): void;
if (foo) { globalThis.foo(); }
const sentinel: number = "";
