declare const c: boolean;
function f() {
    if (c) return { a: 1 };
    return { a: 2, b: "s" };
}
const { a, b } = f();
const r = c ? { x: 1 } : { y: "" };
let arr = [{ p: 1 }, { q: 2 }];
