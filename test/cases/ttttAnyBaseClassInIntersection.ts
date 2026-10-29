// Known failing (missing_checks.md, seed 20261111): a class whose base is any is a non-augmenting subtype normalized to
// any, assignable to any object type on its own, but tsgo rejects it as a member of an intersection source (TS2741)
class Other<T> { constructor(public other: T) {} }
declare const anyB: any;
class B extends anyB {}
declare const b: B;
const o1: Other<string> = b;
declare const b2: B & { x: 1 };
const o2: Other<string> = b2;
