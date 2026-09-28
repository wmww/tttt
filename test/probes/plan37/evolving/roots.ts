// @strict: true
declare function cond(): boolean;
let g;

function f1() {
    let x;
    (x = [], x).push(1);
    return x;
}

function f2() {
    let x;
    (x = []).push("a");
    x;
    let y;
    (y ||= []).push(true);
    return [x, y];
}

function f3() {
    let x = [];
    x?.push(1);
    x.push?.("a");
    return x;
}

function f4() {
    let x;
    x!;
    x = cond() ? "a" : undefined;
    x!;
    x = [];
    x!.push(1);
    x!;
    let y = null;
    y!;
}

function f5() {
    let x;
    x.length;
    x.push(1);
}
