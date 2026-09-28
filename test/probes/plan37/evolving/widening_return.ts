// @strictNullChecks: false
// @noImplicitAny: true
// Without strictNullChecks, what widens to any in an inferred return type is reported: TS7018 in object literals, else
// TS7010/TS7011 with the widened type (tsc's reportErrorsFromWidening / reportWideningErrorsInType)
function f1() { let x = []; x.push(null); return x; }
function f2() { return [null]; }
function f3() { return { a: null }; }
function f4() { return [undefined, null]; }
function f5() { let x = []; x.push(null); x.push(1); return x; }
const f6 = () => [null];
function f7() { return { a: [null] }; }
function f8() { let x = []; return x; }
