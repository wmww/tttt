// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2017", "importHelpers": true, "module": "esnext" } }
// @filename: a.ts
export {};
function f({ ...a: b }: { x: number }): void;
function f(x: any) {}
const sentinel: number = "";
