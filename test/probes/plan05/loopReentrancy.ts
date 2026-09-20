// @strict: true
declare function cond(): boolean;
declare function f(x: string | number): string | number;
declare function g(x: number): string;
declare function g(x: string): number;
declare function g(x: string | number): string | number;
function simple() {
    let x: string | number = 0;
    while (cond()) { x = f(x); }
    x;
}
function overloads() {
    let x: string | number = 0;
    while (cond()) { x = g(x); x; }
    x;
}
function nested() {
    let x: string | number | boolean = 0;
    while (cond()) {
        x;
        while (cond()) { x = typeof x === "number" ? "s" : true; x; }
        x;
    }
    x;
}
function viaConst() {
    let x: string | number = 0;
    while (cond()) { const y = x; x = typeof y === "number" ? "s" : 0; y; }
    x;
}
function arith() {
    let x = 0;
    let s: string | number = "";
    while (cond()) { x = x + 1; s = typeof s === "string" ? x : "a"; s; }
    s;
}
