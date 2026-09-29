// @strict: true
// @target: es2015
interface Column<T extends string> { name: T }
interface Box<T> { v: T }
type A<T> = Box<T extends string ? Column<T> : never>;
type F<T> = Box<[T] extends [string] ? Column<T & string> : never>;
type G<T> = Box<T extends string ? Column<string> : never>;
type H<T> = Box<T extends string ? T : never>;
type I<T> = { v: T extends string ? T : never };
declare const a: A<symbol | string>; const a2: A<string> = a;
declare const f: F<"a" | "b">; const f2: F<"a"> = f;
declare const g: G<symbol | string>; const g2: G<string> = g;
declare const h: H<symbol | string>; const h2: H<string> = h;
declare const i: I<symbol | string>; const i2: I<string> = i;
declare const h3: H<"a" | "b">; const h4: H<"a"> = h3;
