// @strict: true
// tsc: no error; tttt: TS2589 + TS2322 (triage 2026-09-28: the definitely-assignable checks instantiate deep indexed-access chains)
type Values<T> = T[keyof T];
type S = { id?: string; states?: Record<string, S>; };
type Id<T extends S> =
  | (T extends { id: string } ? T['id'] : never)
  | (T['states'] extends Record<string, any>
      ? Values<{ [K in keyof T['states'] & string]: Id<T['states'][K]> }>
      : never);
function h<T extends S, U extends S>(x: Id<U>) { const y: Id<T> = x; }
