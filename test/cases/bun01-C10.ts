// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
import { v } from "x.foo";
const sentinel: number = "";
// @filename: s.d.ts
declare namespace NS { module "*.foo" { export const v: number; } }
