// @strict: true
type KK = "a" | "z";
type O = Omit<any, KK>;
declare const o: O;
const t: { b: string } = o;
const t2: Omit<{ b: string, a: number }, KK> = o;
const u: { b: string } & { c: 1 } = {} as O & { c: 1 };
const u2: Omit<{ b: string, a: number }, KK> & { c: 1 } = {} as O & { c: 1 };
const w: { b: string } = {} as Pick<any, "b" | "c">;
