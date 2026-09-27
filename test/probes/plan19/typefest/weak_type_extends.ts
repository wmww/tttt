// @strict: true
declare const sym: unique symbol;
type EmptyObject = { [sym]?: never };
type A = string extends EmptyObject ? 1 : 2;
type B = { a: string } extends EmptyObject ? 1 : 2;
type C<T> = T extends EmptyObject ? 1 : 2;
type D = C<string>;
type E = C<{ a: string }>;
const s: EmptyObject = "x";
const o: EmptyObject = { a: "x" } as { a: string };
