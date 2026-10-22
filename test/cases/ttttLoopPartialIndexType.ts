// @strict: true
// @noUncheckedIndexedAccess: true
// The loop's first round evaluates `arr[y]` with y's partial type undefined: TS2538, though y's final type is any
// (vue's renderer.ts under noUncheckedIndexedAccess)

function g(arr: number[]) {
    let y;
    while (arr.length) {
        y = arr[y];
    }
}

function h(p: number[], result: number[], k: number) {
    let u, v;
    v = k;
    u = result.length;
    v = result[u - 1];
    while (u-- > 0) {
        result[u] = v;
        v = p[v];
    }
}
