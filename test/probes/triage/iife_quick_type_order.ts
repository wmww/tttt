function g() { return n; }
const n = (z => z + 1)(1);
const m = ((...args) => args)(1, "a");
function h() { return m; }
