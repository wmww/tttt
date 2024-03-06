function foo<T>(t: T): T {
    return t;
}

type xyz = number;
let x = (foo) < xyz > (0);
x = 'hello';

type Baz = <T>(a: T, b: Baz) => void;

function aaa<T>(a: T, b: Baz) {}
