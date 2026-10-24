// rea (morluto/rea, zod v4): a mapped type whose `as` clause is generic for one key (`T[k] extends OptionalOut ? never
// : k` over a shape with one generic member) is generic, but still has the properties of the keys whose names resolve
// (tsc's resolveMappedTypeMembers goes key by key): `provider` is there
// @strict: true
type OptionalOut = { _zod: { optout: "optional" } };
interface S<O> { _zod: { output: O } }
type Out<T extends Record<string, S<unknown>>> = { [k in keyof T as T[k] extends OptionalOut ? never : k]: T[k]["_zod"]["output"] };
type P = { provider: S<string>; other: S<number> };
function f<Schema extends S<unknown>>() {
  let x: Out<P & { result: Schema }> = null!;
  let y: { provider: string } = x;
}
