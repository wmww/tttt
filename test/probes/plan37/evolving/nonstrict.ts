// @strict: false
// @noImplicitAny: true
function f1() {
    let x = [];
    x.push(null);
    x.push("a");
    x.length;
    return x;
}

function f2() {
    let x = [];
    x.push(1);
    x.push(undefined);
    return x;
}

function f3() {
    let x;
    x.push(1);
    let y = [];
    return () => y;
}

export let z = [];
var w = [];
