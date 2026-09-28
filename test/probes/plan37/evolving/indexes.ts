// @strict: true
declare const k: string;
declare const n: number;
declare const u: undefined;
declare const a: any;
enum E { A, B }
declare const e: E;

function f1() {
    let x = [];
    x["a"] = 1;
    x[k] = true;
    x;
    x[n] = "s";
    x[0] = null;
    x[e] = "e";
    x[a] = { a: 1 };
    x[u] = 2;
    return x;
}

function f2() {
    let x = [];
    x[n] += 1;
    x[n]++;
    [x[0]] = [1];
    return x;
}
