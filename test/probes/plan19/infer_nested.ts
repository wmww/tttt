type X<S> = S extends [infer T] ? (string extends T ? 'wide' : 'narrow') : 0;
type A = X<['1']>;
type N<S extends string> = S extends `${infer A}-${infer B}` ? (S extends `${A}${infer U}${B}` ? [A, U, B] : 0) : 0;
type B = N<'foo-bar'>;
type IsUnion<T, U = T> = T extends U ? ([U] extends [T] ? false : true) : never;
type C = IsUnion<1 | 2>;
type D = IsUnion<1>;
