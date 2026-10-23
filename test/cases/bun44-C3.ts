// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
import { Kind } from "./kinds";
const o = { kind };
const sentinel: number = "";
// @filename: kinds.ts
export * as Kind from "./kinds_";
export type Kind = "Field";
// @filename: kinds_.ts
export const FIELD = "Field";
