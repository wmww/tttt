// @strict: true
// An intersection with a type parameter whose constraint is a union is a union once apparent, which isn't compared
// with an object target property by property; unless the union's other members intersect to never with the rest:
// `T & { skip: boolean }` with `T extends Base | undefined` is `Base & { skip: boolean }`, assignable to a weak
// `{ formats?: unknown[] }` (an inference whose candidate is it keeps it, not the constraint)
type Base = { formats: string[] };
declare function lift<U extends { formats?: unknown[] }>(options: U): U;
function impl<T extends Base | undefined>(y: T & { skip: boolean }) {
  const r = lift(y);
  const s: { formats?: unknown[] } = r;
  return r.skip;
}
