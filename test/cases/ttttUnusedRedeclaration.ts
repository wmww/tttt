// @noUnusedLocals: true

// A conflicting declaration is a symbol of its own, not among the locals: only the first is unused
const __VERSION = "1.0.0";
const __VERSION: number = 1;

function f() {
    let b = 1;
    let b = 2;
    var c = 1;
    var c = 2;
    class D {}
    let D = 1;
}
export {};
