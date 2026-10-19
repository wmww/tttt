// @strict: true
// @noEmit: true

// A context sensitive function's contextual signature fixes the type parameter a reverse mapped type in its parameter
// types was inferred for: tsc's instantiateReverseMappedType instantiates the mapped type it reverses, which mentions
// it. The second inference pass then keeps the first pass's inference (arktype's `"child?": ["this", "=>", f]`)
type Infer<D> = { [K in keyof D]: D[K] extends string ? string : D[K] extends readonly [any, any, (x: any) => infer O] ? O : never };
type Elem<T, D> = { [I in keyof T]: I extends "2" ? (In: Infer<D>) => unknown : T[I] };
type Validate<D> = { [K in keyof D]: D[K] extends string ? D[K] : Elem<D[K], D> };
declare function type<const def>(def: Validate<def>): def;

const t = type({ name: "string", child: ["this", "=>", c => c] });
