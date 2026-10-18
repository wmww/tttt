// @declaration: true
// @strict: true
// tsc's isTypeParameterPossiblyReferenced: walking up from a type node to a type parameter's declaration, a Block on
// the way makes the parameter possibly referenced, so a type literal in a generic function's block body is instantiated
// (a new type, its optional properties printed with `| undefined`) even though it mentions no type parameter. An
// expression body keeps the literal itself (better-auth test-utils' suites)
const create = <T>(fn: () => T) => { return (h: { g?: () => any }) => 1; };
export const x = create(() => 1);
const create2 = <T>(fn: () => T) => (h: { g?: () => any }) => 1;
export const y = create2(() => 1);
function create3<T>(fn: () => T) {
    if (Math.random()) {
        const inner = <U>(u: U) => (h: { g?: () => U; k?: () => void }) => 1;
        return inner;
    }
    return null!;
}
export const z = create3(() => "")(1);
// Never-reduced all the same: the literals mention nothing
function reduced<T>() {
    const v: { x: "a" } & { x: "b" } = null!;
    return v;
}
export const w = reduced<number>();
