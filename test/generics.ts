function foo<T>(t: T): T {
    return t;
}

let x = foo(0);
x = 'hello';
