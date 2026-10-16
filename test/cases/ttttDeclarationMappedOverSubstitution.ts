// @declaration: true
// @strict: true
// @target: es2022
// @module: esnext
// A homomorphic mapped type instantiated with a substitution type (`X<S>` in the true branch of `S extends C`) is
// still homomorphic over S: no `S extends infer T ? ... : never` wrapper (tsc's getHomomorphicTypeVariable unwraps it
// with getActualTypeVariable; better-auth's organization client)

// @filename: a.ts
export const f = <O, S2 = undefined>() => {
  type X<T> = { [K in keyof T as T[K] extends { a: unknown } ? K : never]: T[K] };
  type Y<T> = { [K in keyof T]?: T[K] };
  type R = O extends { s: infer S } ? S extends { b: string } ? X<S> | Y<S> : undefined : undefined;
  return {} as unknown as undefined extends S2 ? R : S2;
};
