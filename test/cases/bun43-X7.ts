// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "resolveJsonModule": true, "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
import j from "./j.json";
// @filename: j.json
{"a": }
