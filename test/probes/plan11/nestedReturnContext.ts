// @strict: true
// moduleNameResolver.ts: a nested generic call in a lambda's return is inferred from what the outer call knows
interface MCache<T> { get(k: string): T | undefined; }
interface CacheWithRedirects<K, V> { get(k: K): V | undefined; }
declare function createMCache<T>(): MCache<T>;
declare function getOrCreate<K, V>(c: CacheWithRedirects<K, V>, key: K, create: () => V): V;
function per<T>(m: CacheWithRedirects<string, MCache<T>>) {
    const r: { get(dir: string): MCache<T> } = { get: dir => getOrCreate(m, dir, () => createMCache()) };
    return r;
}
