// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs", "importHelpers": true, "esModuleInterop": true } }
// @filename: a.ts
import type D1, {} from "./m";
export {};
const sentinel: number = "";
// @filename: m.ts
export default class D {}
export const x = 1;
