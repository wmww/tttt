// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
import type { Promise } from "./ty";
new Promise(resolve => resolve());
const sentinel: number = "";
// @filename: ty.ts
export interface Promise<T> { q: T }
