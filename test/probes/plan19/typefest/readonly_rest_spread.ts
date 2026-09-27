type A = readonly [string, ...(readonly number[])];
type B = [string, ...(readonly number[]), boolean];
type C = [...(readonly number[])];
type D = readonly [...(readonly number[])];
declare const a: A; declare const b: B; declare const c: C; declare const d: D;
