interface Foo { a: boolean, b: any, c: { a: undefined, c: null } }

interface Bar { xyz: boolean extends boolean ? string : number }

let a: number = 75;
let b: any = 'hello';
const c: true extends boolean ? 'abc' : 'xyz' = 'abc';

function someFunc(foo, bar: number = 8): void {
    String(foo);
}

let d = 1 * 2 + 3;
let e = 1 + 2 - 3;
let f = 1 + 2 * 3;
let g = +a++;
let h = ((10 + 10) / 2) + 'abc';
let i = someFunc('hello', 2 + 3);

function foo(a, b: number): void {
    String(a)
}

let j = {};
let k = {x: 1, y: 'foo'};
