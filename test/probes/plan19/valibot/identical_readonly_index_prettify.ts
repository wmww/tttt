// @strict: true
// @exactOptionalPropertyTypes: true
type SE<L, R> = (<T>() => T extends (L & T) | T ? true : false) extends <T>() => T extends (R & T) | T ? true : false ? true : false;
type Prettify<T> = { [K in keyof T]: T[K] } & {};
type MarkOptional<T, K extends keyof T> = Partial<T> & Required<Omit<T, K>>;
type R = Record<string, string>;
type M = MarkOptional<R, never>;
type A1 = SE<Prettify<M>, R>;
type A2 = SE<Prettify<Readonly<M>>, Readonly<R>>;
type A3 = SE<Prettify<Readonly<R>>, Readonly<R>>;
type A4 = SE<Prettify<Readonly<Partial<R> & R>>, Readonly<R>>;
type A5 = SE<Prettify<Partial<R> & R>, R>;
type A6 = SE<Prettify<Readonly<Partial<R>>>, Readonly<Partial<R>>>;
type P1 = Prettify<Readonly<M>>;
type P2 = Readonly<M>;
declare const p1: P1;
declare const p2: P2;
const x1 = p1["a"];
const x2 = p2["a"];
