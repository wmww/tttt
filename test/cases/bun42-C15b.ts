// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler", "resolveJsonModule": true } }
// @filename: a.ts
import d from "./d.json";
const sentinel: number = "";
// @filename: d.json
{ "x"
