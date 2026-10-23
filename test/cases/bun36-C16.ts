// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["sub/a.ts"] }
// @filename: sub/a.ts
import {} from "c:/x";
const sentinel: number = "";
// @filename: sub/c:/x.d.ts
export {};
