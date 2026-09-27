// @strict: true
// @exactOptionalPropertyTypes: true
// @target: es2020
interface Options { libraryOptions?: { a: number }; target?: string }
function f(params?: Options) {
    const { libraryOptions, target } = params ?? {};
    return [libraryOptions, target];
}

interface ObjectSchema { properties?: Record<string, number> }
declare const objects: ObjectSchema[];
for (const object of objects) {
    for (const key in object.properties) { key; }
}

export function defineLazy<T, K extends keyof T>(object: T, key: K): T[K] {
    return undefined as T[K];
}

export const RANGES: Record<"a" | "b", [number, number]> = /*@__PURE__*/ (() => ({
    a: [1, 2],
    b: [3, 4],
}))();

const RESOLVING: unique symbol = Symbol("r");
type Resolving = typeof RESOLVING;
interface Z { z: 1 }
const cache = new Map<Z, Z | Resolving>();
function run(s: Z): Z {
    const cached = cache.get(s);
    if (cached === RESOLVING) return s;
    if (cached !== undefined) return cached;
    return s;
}
