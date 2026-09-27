// @strict: true
interface Base { readonly kind: 'schema'; readonly type: string; readonly async: false; readonly run: (x: unknown) => unknown }
interface BaseA { readonly kind: 'schema'; readonly type: string; readonly async: true; readonly run: (x: unknown) => Promise<unknown> }
declare function take<T extends Base | BaseA>(t: T): T;
type First<T extends readonly unknown[]> = T[0];
type WithPipe<TPipe extends readonly [Base, ...unknown[]]> = Omit<First<TPipe>, 'run'> & { readonly pipe: TPipe; readonly run: (x: unknown) => unknown };
function f<const S extends Base, const I extends readonly unknown[]>(p: WithPipe<readonly [S, ...I]>, q: Omit<S, 'run'> & { readonly run: (x: unknown) => unknown }) {
  take(p);
  take(q);
  const k: 'schema' = q.kind;
}
