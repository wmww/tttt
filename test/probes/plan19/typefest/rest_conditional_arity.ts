type MA<A extends boolean> = A extends true ? [] : [1];
declare function f<A extends boolean>(...m: MA<A>): void;
f<false>();
f<true>();
f<false>(1);
type SE<L, R> = (<T>() => T extends (L & T) | T ? true : false) extends <T>() => T extends (R & T) | T ? true : false ? true : false;
declare function g<A, E>(...m: MA<SE<A, E>>): void;
g<{ a: 1 }, { b: 1 }>();
g<{ a: 1 }, { a: 1 }>();
