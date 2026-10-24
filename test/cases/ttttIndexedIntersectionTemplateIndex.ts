// @strict: true
// An intersection indexed by a template literal type is not distributed over its members (tsc's getSimplifiedIndexedAccessType:
// a template literal is Instantiable): `view[`${refName}Obj`]` stays `ServerView<T, Full>[`${T}Obj`]`, comparable to Full
// through `${string}Obj` (akanjs ui/Load/View.tsx's TS2352)
type GetStateObject<T> = {
  [K in keyof T as T[K] extends (...args: never[]) => unknown ? never : K]: T[K];
};
type ServerView<RefName extends string, Model> = { refName: RefName } & {
  [K in `${RefName}Obj`]: GetStateObject<Model>;
};
function Render<T extends string, Full extends { id: string }>(view: ServerView<T, Full>) {
  const { refName } = view;
  const modelObj = view[`${refName}Obj`] as Full;
}
