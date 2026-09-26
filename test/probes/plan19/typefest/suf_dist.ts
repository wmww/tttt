// @strict: true
type IsNever<T> = [T] extends [never] ? true : false;
type A = {a: string};
type U = A | (A & {foo: any});
type SO1<Union> = { [Key in keyof Union]: Union[Key] };
type SO2<Union> = keyof Union extends infer Keys ? { [Key in keyof Union]: Union[Key] } : 0;
type SO3<Union> = keyof Union extends infer Keys ? IsNever<Keys> extends false ? { [Key in keyof Union]: Union[Key] } : 0 : 0;
declare const r1: SO1<U>;
declare const r2: SO2<U>;
declare const r3: SO3<U>;
