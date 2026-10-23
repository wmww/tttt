// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler", "paths": { "*": ["./lib/*"] } }, "files": ["a.ts"] }
// @filename: a.ts
import {} from "x\\";
const sentinel: number = "";
// @filename: lib/x.d.ts
export {};
