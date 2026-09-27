type A = -0;
declare function f<T>(x: T): void;
f<-0>(0);
const z: 0 = -0;
type B = `${-0}`;
type N<T extends number> = `${T}` extends `-${infer U extends number}` ? U : T;
const n: N<-0> = 0;
declare const b: B;
