// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"] }
// @filename: a.ts
import(("./g"));
fromG;
const sentinel: number = "";
// @filename: g.ts
export {};
declare global { var fromG: number }
