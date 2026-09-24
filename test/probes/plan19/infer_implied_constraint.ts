interface Base { kind: string; run(x: unknown): unknown }
type First<T extends readonly [unknown, ...unknown[]]> = T[0];
type SWP<P extends readonly [Base, ...Base[]]> = Omit<First<P>, 'run'> & { readonly pipe: P };
type SWPA<P extends readonly [Base, ...Base[]]> = Omit<First<P>, 'run'> & { readonly pipe: P; async: true };
type X<S> = S extends SWP<infer P> | SWPA<infer P> ? P : 'no';
interface Opt extends Base { kind: 'opt'; wrapped: Base }
type A = X<Opt>;
type Keys<E> = { [K in keyof E]: X<E[K]> extends 'no' ? K : never }[keyof E];
type C = Keys<{ a: Opt; b: Opt }>;
