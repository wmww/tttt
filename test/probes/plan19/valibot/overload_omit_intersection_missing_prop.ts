// @strict: true
type KK = "a" | "z";
interface I { b: string; a: number }
interface G<T> { b: T; a: number }
const t1: Omit<{ b: string, a: number }, KK> = {} as Omit<any, KK>;
const t2: Omit<I, KK> = {} as Omit<any, KK>;
const t3: Omit<G<string>, KK> = {} as Omit<any, KK>;
const t4: Omit<I & { c?: never }, KK> = {} as Omit<any, KK>;
const t5: Omit<I, KK> & { c: 1 } = {} as Omit<any, KK> & { c: 1 };
