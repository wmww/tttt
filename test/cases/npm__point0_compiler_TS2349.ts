// origin: npm @point0/compiler@0.4.2 https://github.com/1gr14/point0 packages/compiler/tsconfig.bench.json seed 20261106
// flip: no
// An instantiation of a mapped type with no members (`Record<never, never>`) isn't an empty anonymous object to typeof
// narrowing: tsc's isEmptyAnonymousObjectType wants ObjectFlags.Anonymous, which a mapped type lacks, so its facts are an
// object's and `typeof x === "function"` drops `EmptyObject & {...}` (never) instead of intersecting it with Function
// @strict: true
type EmptyObject = Record<never, never>;
declare const u: (EmptyObject & { q?: string }) | ((x: number) => boolean);
if (typeof u === 'function') {
    const z: [number] = [u];
    u(1);
}
declare const e: EmptyObject | string;
if (typeof e === 'function') { const z2: [number] = [e]; }
export {};
