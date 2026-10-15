// Effect's Schema.TaggedClass<_A>()(...): the base expression's call relates a tuple against a tuple of another
// target, whose number index relation meets Ann<_A> and needs _A's bases again (TS2310 + TS2506)
// @target: es2022
// @strict: true
interface Ann<A> { pretty?: () => (x: A) => string }
type Annots<Self, A> = readonly [Ann<Self> | undefined, Ann<Self>?, Ann<A>?];
declare function TaggedClass<Self>(): <F extends object>(tag: string, fields: F, annotations?: Annots<Self, F>) => new () => F & { _tag: string };
class _A extends TaggedClass<_A>()("A", { id: 1 }, [undefined, undefined, { pretty: () => (x) => "" }]) {}
