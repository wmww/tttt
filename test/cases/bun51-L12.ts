// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
export type A1 = typeof import("./m").f<>;
export type A2 = typeof import("./m").g<>;
export type A3 = typeof import("./m").v<>;
export type A4 = typeof import("./m")<>;
export const sentinel: number = "";
// @filename: m.ts
export function f<T>(x: T): T { return x; }
export function g(x: number): number { return x; }
export const v = 1;
