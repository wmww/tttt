// @declaration: true
// A reused `typeof this` stands for its this container's symbol (tsc's trackExistingEntityName): an object literal's
// method is accessible only through a visible variable the literal initializes, else the query prints its type
// (headlessui's disposables(): a local object literal reaching itself, TS5088). The symbols baseline has no symbol for
// such a `this` in a type query (a this type, none outside classes)
export const q = { n: 1, m() { return (x: typeof this) => x.n; } }.m;
export const o = {
    n: 1,
    m() { return (x: typeof this) => x.n; },
};
export class C {
    m(x: typeof this) { return x; }
}
