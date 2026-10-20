// An alias whose conditional type relates a class to its base, whose method names a generic type constrained by
// that alias: measuring that type's variances asks for the constraint while the alias is in flight. tsgo's outermost
// variance measurement is a resolution barrier, so that isn't a cycle (no TS2456, no TS2313): the alias is resolved
// again, nested (apollo-client's Cache.Implementation with a TypeOverrides augmentation)
interface TypeOverrides { cache: TestCache }
declare namespace Cache {
  type Implementation = TypeOverrides extends { cache: infer T } ? T extends ApolloCache ? T : "bad" : ApolloCache;
  interface BatchOptions<TCache extends Cache.Implementation, R = void> { update?(cache: TCache): R; }
}
declare abstract class ApolloCache {
  batch<U>(options: Cache.BatchOptions<this, U>): U;
  abstract read(): void;
}
declare class TestCache extends ApolloCache { read(): void; testCacheOnly(): boolean; }
declare const x: Cache.Implementation;
const n: number = x;
