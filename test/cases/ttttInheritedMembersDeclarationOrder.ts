// An interface's members are its own, then the inherited ones by where they are declared (tsgo's getNamedMembers
// sorts them), not base by base: the missing-properties list shows that order
// @strict: true
// @target: esnext
// @lib: esnext,dom
interface Z { z1: 1; z2: 1; }
interface Y { y1: 1; y2: 1; y3: 1; }
interface A extends Y, Z { a: 1; }
const x: A = {};
interface G<T> extends Y, Z { g: T; }
const y: G<string> = {};
declare const c: SVGCircleElement;
const s: HTMLSpanElement = c;
