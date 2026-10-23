// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noErrorTruncation": true } }
// @filename: S136.txt
a.ts(5,7): error TS2322: Type 'null' is not assignable to type '2 | { x: 1; } | (2 & { x: 1; })'.
a.ts(7,7): error TS2322: Type 'null' is not assignable to type '2 | [2 | [unknown]]'.
a.ts(10,7): error TS2322: Type 'null' is not assignable to type '[[unknown]]'.
a.ts(14,7): error TS2322: Type 'null' is not assignable to type '2 | ((2 | 4 | { x: 1; }) & { x: 1; })'.
a.ts(15,7): error TS2322: Type 'null' is not assignable to type '2 | 4 | { x: 1; }'.
a.ts(19,7): error TS2322: Type 'null' is not assignable to type '2 | { x: 1; } | (2 & { x: 1; })'.
a.ts(20,7): error TS2322: Type 'null' is not assignable to type '2 | { x: 1; }'.
a.ts(21,7): error TS2322: Type 'string' is not assignable to type 'number'.
// @filename: a.ts
// A conditional type that is instantiated within its own instantiation, with the same type arguments. The recursion ends at the
// mapped type, whose members are in place (empty) while they are resolved. `root.instantiations[key] = result`: the outer call
// assigns last and returns its own result.
type A<T> = T extends 1 ? { [P in keyof T]: A<T> }[keyof T] & { x: 1 } : 2;
const a1: A<any> = null;
type A2<T> = T extends 1 ? [{ [P in keyof T]: A2<T> }[keyof T]] : 2;
const a2: A2<any> = null;
type S1 = { [k: string]: 1 };
type A3<T> = T extends object ? [{ [P in keyof T]: A3<T> }[keyof T]] : 2;
const a3: A3<S1> = null;
// Through a second alias: that one keeps what it got in the meantime.
type B<T> = T extends 1 ? { [P in keyof T]: B2<T> }[keyof T] & { x: 1 } : 2;
type B2<T> = T extends 3 ? B<T> : 4;
const b1: B<any> = null;
const b2: B2<any> = null;
// The outer call has an alias in its key: the entry without one keeps the result of the inner call.
type C<T> = T extends 1 ? { [P in keyof T]: C<T> }[keyof T] & { x: 1 } : 2;
type CX = C<any>;
const c1: CX = null;
const c2: C<any> = null;
const sentinel: number = '';
