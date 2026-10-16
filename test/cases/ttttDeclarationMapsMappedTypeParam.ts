// @declaration: true
// @declarationMap: true
// @strict: true
// @target: es2022
// @module: esnext
// A mapped type printed from its type maps its type parameter's name to its declaration (tsgo's typeParameterToName;
// redux-toolkit's tests/utils/helpers.tsx, a local alias printed structurally)

// @filename: a.ts
export function f<R>(r: R) {
  type S = Promise<string> & {
    [K in keyof R]: R[K]
  };
  return null as any as S;
}
