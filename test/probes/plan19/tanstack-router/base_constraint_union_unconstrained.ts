// A union with a type variable that has no base constraint has none either (tsc's computeBaseConstraint): F's base
// constraint is unknown, so '/posts' is no literal of F's context and widens (TanStack Router's NavigateFn,
// `TFrom extends RoutePaths<TRouter['routeTree']> | string`, under sloppy flags: TS2353 instead of TS2322), and F
// has no `length`. A conditional's union branch goes the same way, and so does an indexed access whose base access
// is such a union (apparently unknown)
type RP<T> = unknown extends T ? string : 'a' | 'b';
declare function d<R, F extends R | string = string>(o: { from?: F, r?: R }): [F, R];
d({ from: '/posts' }).zzd; // [string, unknown]
declare function a<R, F extends RP<R> | string = string>(o: { from?: F, r?: R }): [F, R];
a({ from: '/posts' }).zza; // ["/posts", unknown]
export function x<R, F extends R | string>(f: F) {
  f.length; // TS2339
}
type Mapped6<K extends string> = { [P in K as `_${P}`]: P };
export function f6<K extends string>(obj: Mapped6<K>, key: keyof Mapped6<K>) {
  const v = obj[key];
  v.zz; // TS2339 on Mapped6<K>[keyof Mapped6<K>]
}
// An await of a union is each member's: Yadda | Awaited<T>
export async function foo<T>(x: T): Promise<T> {
  let yaddable = await getXOrYadda(x);
  return yaddable;
}
interface Yadda { stuff: string }
declare function getXOrYadda<T>(x: T): T | Yadda;
