// @strict: true
// The required properties an intersection source's optional property check asks for are looked up member by member
// (tsc's getPropertyOfType on the intersection), not only in its apparent type: U's values being any makes
// `S[string] & U[string]` apparent any, but S's values' properties are there (zod's util.Extend, TS2344)
interface Z { _zod: 1; b: 2 }
type L = Readonly<{ [k: string]: Z }>;
function f<S extends L, U extends Record<string, any>>(x: S[string] & U[string], y: S[keyof S] & U[keyof U]) {
  const z: Z = x;
  const z2: Z = y;
}
