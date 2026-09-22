const o: { a: boolean } = { a: true };
const p: { a: true } | { a: false } = { a: true };
declare function f(x: { a: boolean }): void;
f({ a: false });
