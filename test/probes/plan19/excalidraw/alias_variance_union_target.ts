// @strict: true
type Mutable<T> = { -readonly [P in keyof T]: T[P] };
type A = Readonly<{ type: "a"; x: number }>;
type B = Readonly<{ type: "b"; x: number; points: number[] }>;
type El = A | B;
declare function bump<T extends Mutable<El>>(e: T): T;
function f<E extends El>(e: Mutable<E>) {
  bump(e);
  const m: Mutable<El> = e;
}
