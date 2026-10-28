// @noUnusedLocals: true
// @noUnusedParameters: true
// @noEmit: true
// tsgo's reportUnused skips a declaration with a node in it that its parser finished first after a parse error
// (ThisNodeHasError): a missing expression, type or name is such a node, so is the next node after skipped tokens;
// an error after a declaration finished doesn't hide it (agents/bugs/bun_others.md)
export function p01_f() {
    let a = 1;
    let b = (1 +);
    let c = 2;
}
export class p02_C {
    private x = (1 +);
    private y = 1;
    private z() {}
}
export function p05_h(a, b: , c) {}
export function p05_h2(d: string, e = ) {}
export function p06_g() {
    let q = 1;; )
    let r = 2;
    let s = 3;
}
export function p07_outer() {
    )
    function inner() {}
    function inner2() {}
}
declare const o: any;
export function p08_d() {
    const { a, b: } = o;
    const [c, , e] = o;
}
export const p09_f = (a, b) => { let z = ; let w = 1; };
export const p09_g = (m: number, ) => { let v = 1; };
export function p10_k() {
    enum E { A = (1 +) }
    namespace N { }
    interface I { x: }
    type T = ;
    class K { }
}
export class p11_D<T, U> {
    x: U = (1 +);
}
export class p11_D2<T> { m<V>(a: ) {} }
export function p12_m() {
    for (const v of ) {}
    for (const w in {}) { let u = }
}
export function p13_s() {
    let s1 = "abc
    let s2 = 2;
    let n = 1n2;
    let t = 3;
}
export function p14_u() {
    let a = 1 2;
    let b = [1, 2;
    let c = 3
    let d = {x: 1 y: 2};
    let e = 5;
}
export function p15_v() {
    switch (1) {
        case 1 let z = 1;
        default?:
            let y = 2;
    }
    try { } catch (err { }
    let after = 1;
}
