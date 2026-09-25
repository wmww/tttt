// @strict: true
declare const b: { x: number };
function f1(x?: typeof b, y: typeof b = b, z?: string | undefined) {}
interface P { p?: typeof b; q: typeof b | undefined }
declare let p: P;
class K { k?: typeof b; }
function f2(this: typeof b, x: typeof b) {}
function f3(x: typeof b): x is typeof b { return true; }
type A<T> = { a: T; f(x: typeof b): T };
declare let a: A<number>;
declare function f4<T>(x: T, y: typeof b): typeof b;
f4(1, b);
