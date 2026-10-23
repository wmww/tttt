// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022", "module": "esnext", "moduleResolution": "bundler", "jsx": "preserve" } }
// @filename: a.ts
const m1 = import.#constructor;
const m2 = import.#meta;
class K { #a = 1; m() { return import.#a; } }
const sentinel: number = "";
