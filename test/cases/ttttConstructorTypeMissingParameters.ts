// `new` in a type starts a constructor type whatever follows (tsc's isStartOfFunctionTypeOrConstructorType):
// `new>` is missing its `(`, in a type and in type arguments
declare function f<T>(x: any): void;
declare const x: any;
let t: new>(x);
var v = f<new>(x);
var w = f<abstract new>(x);
