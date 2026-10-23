// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
namespace NS {
  type LU = Partial<{ a: 1 } | { b: 1 }>; type GT<T> = [LU, T];
  const n1: never = null! as [LU] | [GT<1>[0]];
}
{
  type LU = Partial<{ a: 1 } | { b: 1 }>; type GT<T> = [LU, T];
  const n2: never = null! as [LU] | [GT<1>[0]];
}
// at the top level of the file the alias is isNonGenericTopLevelType: one type in both
type TU = Partial<{ a: 1 } | { b: 1 }>; type TGT<T> = [TU, T];
const n3: never = null! as [TU] | [TGT<1>[0]];
const sentinel: number = "";
