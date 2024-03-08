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

function show(a, b: number): void {
    console.log(a);
    console.log(b);
}

function show hello;

let j = {};
let k = {x: 1, y: {null_: null, three: 3}};
show(a, b);
k.x;
k.y.three;

let l = () => 'arrow func';
console.log(l());

let m: (a: number) => void;
let n: <T>(a: T) => void;
let o: (x: any, y: number) => void = show;
let p: (a: 0) => string = (a: number) => 'foobar';
console.log(p(0));
