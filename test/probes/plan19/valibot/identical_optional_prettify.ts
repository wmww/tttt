// @strict: true
// @exactOptionalPropertyTypes: true
type SE<L, R> = (<T>() => T extends (L & T) | T ? true : false) extends <T>() => T extends (R & T) | T ? true : false ? true : false;
type Prettify<T> = { [K in keyof T]: T[K] } & {};
type MarkOptional<T, K extends keyof T> = Partial<T> & Required<Omit<T, K>>;
type O = { key1: string; key2: number };
type A1 = SE<Prettify<MarkOptional<O, 'key1' | 'key2'>>, { key1?: string; key2?: number }>;
type A2 = SE<Prettify<MarkOptional<O, 'key1'>>, { key1?: string; key2: number }>;
type A3 = SE<Prettify<Partial<O>>, { key1?: string; key2?: number }>;
type A4 = SE<Partial<O>, { key1?: string; key2?: number }>;
type A5 = SE<Prettify<Partial<O> & {}>, { key1?: string; key2?: number }>;
type A6 = SE<Prettify<Partial<O> & Required<{}>>, { key1?: string; key2?: number }>;
