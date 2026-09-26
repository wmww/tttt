declare const queries: { a: number };
export type B<Q> = Q extends typeof queries
  ? {
      a<T>(x: T): T
      b<T>(x: T): T
    }
  : never
