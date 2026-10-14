// An unresolved type reference's error type is named by tsc's unresolved symbol (getUnresolvedSymbolForEntityName),
// so it sorts before the unnamed any (CompareTypes: compareTypeNames), as a union member or a type argument; which
// member comes first also decides a contextual union signature's parameters (getContextualSignature takes the first
// member's): babel__traverse's `path` is `NodePath<t.Aliases>` with @babel/types unresolved
// @filename: m.ts
import * as t from "nope";
declare class NP<T> { type: T }
declare const u: NP<any> | NP<t.Aliases>;
declare const v: NP<Missing> | NP<any>;
export const a = [u, v];
type F<P> = (path: NP<P>) => void;
const e: F<any> | F<t.Aliases> = (path) => { path.type; };
