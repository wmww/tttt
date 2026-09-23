// @strict: true
interface A { kind: "a"; a: number }
interface B { kind: "b"; b: string }
declare function isA(x: A | B): x is A;
declare function takesA(a: A): void;
declare function takesB(b: B): void;
export function f<T extends A | B>(x: T) {
    isA(x) ? takesA(x) : takesB(x);
}
