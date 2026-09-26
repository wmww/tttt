// @strict: true
type O = { (foo: number): string; (foo: string, bar: number): number; };
type NWO = O & { baz: boolean[] };
type R1 = NWO extends {(...a: any): unknown; (...a: any): unknown} ? true : false;
type H<T> = T extends {(...a: infer A): unknown; (...a: infer B): unknown} ? [A, B] : false;
type R2 = H<NWO>;
type H1<T> = T extends {(...a: infer A): unknown} ? A : false;
type R3 = H1<NWO>;
