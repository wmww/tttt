declare namespace q { type G<T> = (x: T) => T }
type Bound<T> = T
export type B<Q> = Q extends string
  ? {
      a<T extends HTMLElement = HTMLElement>(
        ...args: Parameters<Bound<q.G<T>>>
      ): ReturnType<q.G<T>>
      b<T extends HTMLElement = HTMLElement>(
        ...args: Parameters<Bound<q.G<T>>>
      ): ReturnType<q.G<T>>
    } & { [P in keyof Q]: Q[P] }
  : never
