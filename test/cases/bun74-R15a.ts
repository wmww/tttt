// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noUnusedParameters": true } }
// @filename: a.ts
export const e1: 0 = (a: unknown): a is string => true;
export const e2: 0 = <T>(a: T): asserts a is T => {};
export const e3: 0 = { f(a: unknown): a is string { return true; } };
declare function take(n: 0): void; take(function (a: unknown, b: unknown): b is string { return true; });
export const e5: (a: unknown) => a is number = (a: unknown): a is string => true;
// controls: tsc reports these too
export const c1 = (a: unknown): a is string => true;
export const c2: 0 = [(a: unknown): a is string => true];
export function c3(a: unknown): a is string { return true; } export const c4: 0 = c3;
const sentinel: number = "";
