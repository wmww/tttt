// @strict: true
// A destructuring's object rest has the binding's symbol (tsc's getRestType), which orders it in a union by that
// binding's declaration: before an object literal written later. Subtype reduction (an array literal's element type)
// checks the members from the last on, so the literal is dropped as a subtype of `g` before `g` is dropped as one of
// `e`; with the rest types ordered last, `g` went first and the literal stayed (method parameters are bivariant, so
// the literal is a subtype of `g` without being one of `e`). base-ui's CheckboxRoot, TS2322
interface A { a: 1 }
interface B { a: 1; c: 1 }
interface C { c: 1 }
interface P { id?: string; m?(e: A): void; extra?: number; x?: 1; y?: 2 }
interface Q { id?: string; m?(e: B): void; extra?: number; y?: 2; z?: 3; w?: 4 }
function f(p: P, q: Q) {
    const { x, ...e } = p;
    const { w, ...g } = q;
    const arr: (typeof e)[] = [{ id: "s", m(event: C) {} }, e, g];
    const arr2: number = [{ id: "s", m(event: C) {} }, e, g];
    return [x, w, arr, arr2];
}
