// @strict: true
// @noEmit: true
// An iterable whose iterator's methods are wrong (TS2490, TS2767) is reported only if the first time its iteration
// types are worked out for a use (tsc's iterationTypesCache key: the type and whether it is for-of / async) has an
// error node: control flow's for-of variable type and a destructuring assignment's rest target ask without one and
// silence it (bun 99-V4 checkIteratedTypeOrElementType__01/02), a for-of and a spread are two keys (__06), and a type
// parameter's iterator is its constraint's (04-C2a)
export {}
interface Loose { [Symbol.iterator](): { next(): {} } }
declare const its: Loose;
declare function pick(): string | number;
const early = g();
function g() {
    let v: string | number = pick();
    for (const x of its) {
        x.go(v);
        return v;
    }
    return 0;
}
interface Loose2 { [Symbol.iterator](): { next(): {} } }
declare let rest: Loose2;
declare const other: Loose2;
[...rest] = [1, 2];
const copy = [...other];
interface Loose3 { [Symbol.iterator](): { next(): {} } }
declare const first: Loose3;
declare const second: Loose3;
for (const item of first) { item; }
const copy2 = [...second];
type BadReturn = { [Symbol.iterator](): { next(): IteratorResult<number>; return: number } };
function f1<T extends BadReturn>(t: T) { for (const x of t) {} }
