// A generic alias referenced through an import goes by the type arguments as written, defaults not filled in (tsgo's
// getTypeFromTypeAliasReference): `C<T>` and `C<T, {}>` are two types that print as written, conditional or union
// @filename: a.ts
export type C<T, O extends object = {}> = T extends string ? [T, O] : T;
export type U<T, O = {}> = T | O | undefined;
// @filename: main.ts
import type { C, U } from "./a";
function boom<T>(x: C<T>, y: C<T, {}>, z: U<T>, w: U<T, {}>) {
    x = y
    y = x
    z = w
}
