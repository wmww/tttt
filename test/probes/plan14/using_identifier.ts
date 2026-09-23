// @target: esnext
// @lib: esnext
declare function using(x: number): number;
var usingArr: number[] = [];
let x = using(1);
declare let y: any;
function f(using: number[]) {
    using[0];
    using
    y = 1;
    for (using of [[1]]) {}
    for (using in {}) {}
    return using;
}
{
    using a = null, b = undefined;
    using as = null;
}
