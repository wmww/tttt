// @allowJs: true
// @checkJs: true
// @noEmit: true
// @filename: a.js
function f1() {
    var x = [];
    x.push(1);
    x.push("a");
    return x;
}
function f2() {
    let x;
    x = [];
    x[0] = true;
    return x;
}
function f3() {
    const x = [];
    return () => x;
}
