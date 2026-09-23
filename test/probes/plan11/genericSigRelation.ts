// @strict: true
// compareSignaturesRelated: a generic source signature is instantiated in the context of the target
declare let f1: <T>(x: T) => T;
declare let f2: <T>(x: T[]) => T;
const n1: (x: number) => number = f1;
const n2: (x: string[]) => string = f2;
const bad: (x: string[]) => number = f2;
const u: <U>(x: U, y: U) => U = f1;
declare let g1: <T extends { a: string }>(x: T) => T["a"];
const g2: (x: { a: string; b: number }) => string = g1;
interface Box<T> { value: T }
const h: <S>(x: Box<S>) => S = <T>(b: Box<T>) => b.value;
