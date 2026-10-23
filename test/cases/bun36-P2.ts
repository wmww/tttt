// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler", "paths": { "*": ["./src/*"] } }, "files": ["a.ts"] }
// @filename: a.ts
import {} from "";
const sentinel: number = "";
// @filename: src/index.ts
export {};
