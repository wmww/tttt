function foo<T>(t: T): T {
    return t;
}

function baz() {
    return foo;
}
let x = baz()(7);
