// @strict: true
type H<T> = T[keyof T];
type P<U extends {x: string}> = H<{a: U["x"]}>;
declare function f<U extends {x: string}>(u: U): P<U>;
const r: 1 = f({x: "s"}) ;
