// @strict: true
// A property assigned through a `!` or a spread is read with its read type (tsc's isWriteOnlyAccess is false there);
// a direct target, compound ones too, takes the setter's
class A { get r(): string { return "" } set r(v: string | number) { } get p(): string[] { return [] } set p(v: number[] | string[]) { } }
declare const a: A;
a.r! = 1;
(a.r) = 1;
a.r = 1;
[...a.p] = [1, 2];
[a.r] = [1];
({ x: a.r } = { x: 1 });
a.r ??= 1;
a.r ||= 1;
