// @module: esnext
// @moduleResolution: bundler
// `typeof import("./m").f<...>` is an instantiation expression (tsc's getTypeFromImportTypeNode,
// getInstantiationExpressionType): TS2635 for what has no signature taking the type arguments, an empty list `<>`
// taking any generic one (hasCorrectTypeArgumentArity), at the `<`'s end then
// @filename: m.ts
export function f<T>(x: T): T { return x; }
export function g(x: number): number { return x; }
export const v = 1;
// @filename: a.ts
export type A1 = typeof import("./m").f<>;
export type A2 = typeof import("./m").g<>;
export type A3 = typeof import("./m").v<>;
export type A4 = typeof import("./m")<>;
export type B1 = typeof import("./m").f<string>;
export type B2 = typeof import("./m").g<string>;
export type B3 = typeof import("./m").v<string>;
