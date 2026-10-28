// @noUnusedLocals: true
// @noUnusedParameters: true
// @noEmit: true
// tsgo's reportUnused location for imports is the import clause (TS6192) or the specifier, for type parameters their
// declaration: a parse error after the clause (its module specifier) doesn't hide the clause's names
import { a, b as } from "m";
import x, { y } from ;
import * as ns from "n"
export type A<T, U> = { x: };
export type B<V> = V extends Array<infer W ? 1 : 2;
type Q<R> = R;
