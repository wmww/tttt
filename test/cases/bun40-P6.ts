// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs", "moduleResolution": "bundler", "resolvePackageJsonExports": false, "resolvePackageJsonImports": false, "traceResolution": true }, "files": ["a.ts"] }
// @filename: a.ts
import "./b";
const sentinel: number = "";
// @filename: b.ts
export {};
