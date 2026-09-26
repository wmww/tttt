// @strict: true
type X<T> = T extends unknown ? T : never;
type Y<T> = T extends unknown ? T : T;
type Z<T, U = unknown> = T extends never ? T : T extends U ? 1 : T;
declare const a: X<string | number>;
declare const b: Y<string | number>;
declare const c: Z<string | number>;
declare const d: Z<string | number, object>;
