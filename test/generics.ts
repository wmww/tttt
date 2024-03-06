function foo<T extends number>(t: T): T {
    return t;
}

let x = foo(7);
x = 'hello'
