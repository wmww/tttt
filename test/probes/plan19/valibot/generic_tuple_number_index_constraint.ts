// @strict: true
interface I { t?: { issue: string } }
type Issue<T extends I> = NonNullable<T['t']>['issue'];
function f<S extends I, const T extends readonly I[]>(a: Issue<(readonly [S, ...T])[number]>, b: (readonly [S, ...T])[number], c: T[number]) {
  const x: string = a;
  const y: I = b;
  const z: I = c;
}
