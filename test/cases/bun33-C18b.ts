// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
import.foo<1>("./m");
const sentinel: number = "";
// @filename: m.ts
export const m = 1;
