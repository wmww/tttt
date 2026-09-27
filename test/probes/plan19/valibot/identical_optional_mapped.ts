// @strict: true
// @exactOptionalPropertyTypes: true
type SE<L, R> = (<T>() => T extends (L & T) | T ? true : false) extends <T>() => T extends (R & T) | T ? true : false ? true : false;
type Prettify<T> = { [K in keyof T]: T[K] };
type O = { key1: string };
type P = Prettify<Partial<O>>;
declare const p: P;
const v = p.key1;
type V = P["key1"];
type A3 = SE<P, { key1?: string }>;
type A4 = SE<P, { key1?: string | undefined }>;
