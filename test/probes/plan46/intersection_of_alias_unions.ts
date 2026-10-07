interface A { a: 1 }
interface B { b: 1 }
type U1 = A | B;
type U2 = A | B;
declare const x: U1 & U2;
declare const y: U1 & U1;
type K<T> = keyof T;
declare const z: K<{ p: 1, q: 1 }> & keyof { p: 2, q: 2 };
