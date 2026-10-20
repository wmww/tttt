// @strict: true
// A homomorphic mapped type over an intersection printed as `X extends infer T ? { [K in keyof T]: ... } : never` is
// parenthesized as a union member (apollo-client's Prettify in an optional property)
type Prettify<T> = { [K in keyof T]: T[K] } & {};
type F<V, O extends { v?: unknown }> = { v?: Prettify<V & { [K in keyof O["v"]]: 1 }> };
declare const f: F<string, {}>;
function g<V, O extends { v?: unknown }>(x: F<V, O>) { return x.v; }
