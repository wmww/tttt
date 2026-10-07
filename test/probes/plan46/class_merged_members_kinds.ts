class F { m() {} m: number; get g() { return 1 } g: string; static s() {} static s: number; }
declare const f: F;
f.m; f.g; F.s;
interface G<T> { a: T; b(): T }
class G<T> { a: T; b(): T { return null as any } c: T }
interface G<T> { c: string }
declare const g: G<number>;
g.a; g.b; g.c;
