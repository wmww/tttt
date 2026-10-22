// @strict: true
// A destructuring default is unioned with the property's type subtype-reduced before it is widened (tsc's
// getTypeForBindingElement): an object literal in a union default (`c ? { a: true } : undefined`) that fits a type
// with optional properties is absorbed, so indexing the result with a key of either property is no TS7053/TS2339
type Key = 'a' | 'b';
interface Options {
    flags?: Partial<Record<Key, boolean | { n?: number }>>;
    label?: string;
}
declare const options: Options;
declare const key: Key;

const { label, flags = !label ? { a: true } : undefined } = options;
const v = flags?.[key];
const w = flags?.b;

let { flags: m = label ? undefined : { b: false } } = options;
m?.a;

function f({ flags: p = label ? { a: true } : undefined }: Options) {
    return p?.[key];
}
