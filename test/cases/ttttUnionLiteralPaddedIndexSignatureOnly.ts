// `(p || {}).c` reads undefined from the object literal only if a member has the property itself: when the others
// only have index signatures, tsc's createUnionOrIntersectionProperty makes no property, and the union's index
// signatures, which `{}` lacks, are read: TS2339 (react-navigation's `previousDescriptor?.options || {}`, options
// being `Omit<Unresolved, K> & {...}`)
// @strict: true
import type { D } from "nope";
declare const o: { [k: string]: number } | undefined;
const { c } = o || {};
const d = (o || {}).c;
declare const p: { [k: string]: number; x: number } | undefined;
const { c: e } = p || {};
const f = (p || {}).c;
const g = (p || {}).x;
type O = Omit<D, "a"> & { b: 1 };
declare const q: O | undefined;
const h = (q || {}).c;
const { c: i } = q || {};
const j = (q || {}).b;
type M = { [k: string]: number } & { b: 1 };
declare const m: M | undefined;
const k = (m || {}).c;
