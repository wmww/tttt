// @strict: true
// tsc: P : U["x"]   tttt: unknown (ty_indexed_instantiated mistakes a resolved property type that is itself an
// indexed access for an unresolvable access)
type H<T> = T[keyof T];
type P<U extends {x: string}> = H<{a: U["x"]}>;
