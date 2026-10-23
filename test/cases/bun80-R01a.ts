// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022", "module": "esnext", "moduleResolution": "bundler", "jsx": "preserve" } }
// @filename: a.ts
type T1 = typeof import("./x", { with: { #constructor: "a" } });
type T2 = import("./x", { with: { #constructor: "a" } }).A;
import { a as a3 } from "./x" with { #constructor: "a" };
export * from "./x" with { #constructor: "b" };
const sentinel: number = "";
// @filename: x.ts
export const a = 1; export type A = number;
