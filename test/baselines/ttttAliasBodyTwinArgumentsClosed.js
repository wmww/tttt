//// [test/cases/ttttAliasBodyTwinArgumentsClosed.ts] ////

//// [ttttAliasBodyTwinArgumentsClosed.ts]
// An alias body's mapped twin turns back into the ordinary instance under a mapper only where tsc's instance could
// contain type variables, as its type arguments say (getObjectTypeInstantiation's carried flag): `Readonly<InternalSlots>`
// of a non-generic alias, an interface or an instantiated alias stays the alias (vue's `$slots: Slots`), a type literal's doesn't
type InternalSlots = { [name: string]: (() => void) | undefined };
interface Foo { a: number }
type G<T> = { g: T };
export type Slots = Readonly<InternalSlots>;
export type Action = Readonly<{ type: string }>;
export type F = Readonly<Foo>;
export type GA = Readonly<G<number>>;
export type AR = Readonly<Array<{ a: 1 }>>;
export type U = Partial<InternalSlots | Foo>;
export type U2 = Partial<Foo | { b: 1 }>;
interface Box<P> { a(p: P): Slots; b(p: P): Action; c(p: P): F; d(p: P): GA; e(p: P): AR; f(p: P): U; g(p: P): U2 }
declare const b: Box<string>;
export const ya = b.a, yb = b.b, yc = b.c, yd = b.d, ye = b.e, yf = b.f, yg = b.g;


//// [ttttAliasBodyTwinArgumentsClosed.js]
export const ya = b.a, yb = b.b, yc = b.c, yd = b.d, ye = b.e, yf = b.f, yg = b.g;


//// [ttttAliasBodyTwinArgumentsClosed.d.ts]
type InternalSlots = {
    [name: string]: (() => void) | undefined;
};
interface Foo {
    a: number;
}
type G<T> = {
    g: T;
};
export type Slots = Readonly<InternalSlots>;
export type Action = Readonly<{
    type: string;
}>;
export type F = Readonly<Foo>;
export type GA = Readonly<G<number>>;
export type AR = Readonly<Array<{
    a: 1;
}>>;
export type U = Partial<InternalSlots | Foo>;
export type U2 = Partial<Foo | {
    b: 1;
}>;
export declare const ya: (p: string) => Slots, yb: (p: string) => Readonly<{
    type: string;
}>, yc: (p: string) => F, yd: (p: string) => GA, ye: (p: string) => AR, yf: (p: string) => U, yg: (p: string) => U2;
export {};
