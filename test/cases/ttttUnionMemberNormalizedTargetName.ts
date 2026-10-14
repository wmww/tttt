// @strict: true
// @noEmit: true
// A union source's failing member names the target as tsc's isRelatedTo normalized it before eachTypeRelatedToType:
// a deferred reference (an alias' tuple or array body) without the alias name, below a head that names it
type P = [string, number];
declare let e: (string[] | [string, number]);
let g: P = e;
interface B { a: string }
interface I extends B {}
declare let k: (number | { a: string });
let l: I = k;
type A = string[];
declare let m: (number[] | string[]);
let n: A = m;
