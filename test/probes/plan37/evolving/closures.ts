// @strict: true
declare function cond(): boolean;

function f1() {
    const x = [];
    x.push(1);
    const g = () => x;
    x.push("a");
    return [g, x];
}

function f2() {
    let x = [];
    x.push(1);
    function g() { return x; }
    return g;
}

function f3() {
    const x = [];
    const h = () => { x.push(1); return x; };
    x;
    return h;
}

function f4() {
    let x = [];
    x.push(1);
    return () => x.length;
}
