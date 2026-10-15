// @declaration: true
// @strict: true
// A homomorphic mapped type over a deferred conditional type has the properties of the conditional's constraint
// (tsc's resolveMappedTypeMembers over getPropertiesOfType of the apparent modifiers type): a union's common ones,
// an intersection's, whatever their genericity. `Readonly<CD<T>>["id"]` was an error type
declare const kData: unique symbol;
type R<O extends keyof D | '', D> = { [K in keyof D]: K extends O ? unknown : D[K] };
declare abstract class S<D extends {}, O extends keyof D | '' = ''> {
	protected [kData]: Readonly<R<O, D>>;
}
type A = { type: 1; id: string } | { type: 2; id: string };
type CD<T extends 1 | 2 | 'u'> = T extends 1 | 2 ? Extract<A, { type: T }> : { id: string };
export class C<T extends 1 | 2 | "u" = "u", O extends keyof CD<T> | "" = ""> extends S<CD<T>, O> {
    get id() { return this[kData].id; }
}
type CE<T> = T extends 1 ? A : { id: string };
export function f<T extends 1 | 2 | "u", O extends keyof CD<T> | "">(a: Readonly<R<O, CD<T>>>, b: R<O, CD<T>>, c: Readonly<CE<T>>,
    d: { [K in keyof CE<T>]: CE<T>[K] }, g: Readonly<Extract<{ type: 1; id: string }, { type: T }>>, h: Readonly<Extract<A, { type: T }>>) {
    return [a.id, b.id, c.id, d.id, g.id, h.id] as const;
}
