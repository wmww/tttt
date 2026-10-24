// A destructured element the pattern's narrowing leaves generic (`T & Exclude<T, "c">`, the parent narrowed by its
// discriminant) is made narrowable after that, like a declared type (tsc's checkIdentifier: getNarrowedTypeOfSymbol, then
// getNarrowableTypeForReference): passed to `never` in a switch's default it's its constraint narrowed to never (coop)
// @strict: true
type K = 'a' | 'b' | 'c';
type Q<T extends K> = { h: T; u: string } & ({ h: Exclude<T, 'c'> } | { h: 'c' });
declare function never_(x: never): never;
function f<T extends K>(q: Q<T>) {
  const { h, u } = q;
  switch (h) {
    case 'a': return 1;
    case 'b': return 2;
    case 'c': return 3;
    default: never_(h);
  }
}
function g<T extends K>(q: Q<T>) {
  const h = q.h;
  switch (h) {
    case 'a': return 1;
    case 'b': return 2;
    case 'c': return 3;
    default: never_(h);
  }
}
