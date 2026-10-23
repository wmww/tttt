// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler", "traceResolution": true }, "files": ["a.ts", "b.ts"] }
// @filename: a.ts
import {} from "./dir";
const sentinel: number = "";
// @filename: b.ts
import {} from "./dir";
// @filename: dir/index.d.ts
export {};
