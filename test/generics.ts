function foo<T>(t: T): T {
    return t
}

let bar = foo(0);
bar = 'hello'; // error
