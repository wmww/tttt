function foo<T>(t: T): T {
    return t;
}

let x = foo(0);
x = 'hello';
let a: (a: number) => void;
let b: <T>(a: T) => void;
