// Known failing (agents/bugs/type_core.md "tail recursion's deferred result is a fresh type"): tsgo's getConditionalType
// returns a new, uncached conditional type when a tail-recursive step ends deferred, so the chain
// FromSchema<C, C[keyof C]> -> FromSchema<C, C[string | number | symbol]> -> FromSchema<C, C[string]> -> (tail)
// FromSchema<C, C[keyof C]> reaches a fresh copy, and the base constraint's cycle starts below the outer type, which
// keeps a constraint. tttt interns the copy as the outer type itself: circular, no constraint, spurious TS2344
type Type = { values: string };
type Wrapped<T extends Type> = { values: T["values"] };
type FromObject<Context extends Record<string, Type>, Props, Shape extends Record<keyof Props, Type> = {
  [Key in keyof Props]: Wrapped<FromSchema<Context, Props[Key]>>;
}> = Shape;
type FromSchema<Context extends Record<string, Type>, S> =
  S extends Type ? FromSchema<Context, Context[keyof Context]> : never;
function f<C extends Record<string, Type>>(x: FromSchema<C, C[keyof C]>["values"]): string { return x; }
