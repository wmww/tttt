// Eleven levels of one anonymous type deep the next prints as `any` and the rest of the print is past the truncation
// length (tsgo: visitAndTransformType sets context.truncating): later members, list items and union members elide.
declare function wrap<T>(x: T): { inner: T; o: { a: 1; b: 2; c: 3; d: 4; e: 5; f: 6 }; l: [1, 2, 3, 4, 5, 6]; u: 1 | 2 | 3 | 4 | 5 | 6; g: Array<number>; z: string };
const w = wrap(wrap(wrap(wrap(wrap(wrap(wrap(wrap(wrap(wrap(wrap(wrap(wrap(0)))))))))))));
declare function pair<T>(x: T): [T, { a: 1; b: 2 }, 3, 4, 5, 6, 7];
const p = pair(pair(pair(pair(pair(pair(pair(pair(pair(pair(pair(pair(0))))))))))));
declare function un<T>(x: T): { x: T } | { a: 1 } | { b: 2 } | { c: 3 } | { d: 4 } | { e: 5 };
const q = un(un(un(un(un(un(un(un(un(un(un(un(0))))))))))));
const e: number = q;
