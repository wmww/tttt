// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
export {};
import.defer?.("./nope");
import.defer?.<string>("./nope");
const x1: number = import.defer?.("./b");
const x2: number = import.defer("./b");
const sentinel: number = "";
// @filename: b.ts
export const b = 1;
