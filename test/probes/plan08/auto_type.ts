// @strict: true
declare function more(): boolean;
export function f() {
    let x;
    const a = x;
    if (more()) x = 1;
    else x = "s";
    const b = x;
    let y = null;
    const c = y;
    y = 3;
    const d = y;
    let z;
    const g = () => z;
    z = 1;
    var w;
    for (const i of [1, 2]) {
        const e = w;
        w = i;
    }
    let u;
    u += 1;
    const h = u;
}
