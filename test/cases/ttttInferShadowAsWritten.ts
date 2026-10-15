// @declaration: true
// @strict: true
// An annotation the declaration transformer copies as written keeps its `infer` names, even when they shadow a type
// parameter in scope; a type the node builder prints (an inferred one) renames them (T_1)
export type A<T> = T extends { a: infer T } ? T : never;
export type B<T> = T extends { a: infer T extends string } ? T : never;
export type C<T> = T extends [infer U, infer U] ? U : never;
export declare const g: <T>(x: T) => T extends { a: infer T } ? T : never;
export const h = <T,>(x: T) => null! as T extends { a: infer T } ? T : never;
export const k = (x: <T>(a: T) => T extends {a: infer T} ? T : never) => x;
export function k2<T>(x: T extends {a: infer T} ? T : never) { return x; }
export function k3<T>(x: { p: T extends {a: infer T} ? T : never }) { return x.p; }
export class C<T> { m(x: T extends {a: infer T} ? T : never) { return x; } }
