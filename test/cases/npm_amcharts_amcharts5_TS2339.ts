// origin: npm amcharts/amcharts5@master https://github.com/amcharts/amcharts5 tsconfig.json seed 20261400
// flip: no
// @strict: true
// @noEmit: true
// @target: es2015
// @lib: es2020
// amcharts' $object.entries: `Array<[Keyof<O>, O[Keyof<O>]]>` with `Keyof<T> = Extract<keyof T, string>`, over a
// number-indexed `{ [id: number]: IParent }`. tsgo types the element as `[never, IParent]` (the indexed access keeps
// the index signature's type), tttt as `[never, never]`, so every use of `parent` is TS2339 on never (35 errors
// on the project, TS2322/TS2345 among them).
type Keyof<T> = Extract<keyof T, string>;
function entries<O>(object: O): Array<[Keyof<O>, O[Keyof<O>]]> {
  return Object.entries(object as any) as Array<[Keyof<O>, O[Keyof<O>]]>;
}
interface IParent { uid: number; isDisposed(): boolean }
export class Root {
  protected _dirtyParents: { [id: number]: IParent } = {};
  run() {
    entries(this._dirtyParents).forEach(([key, parent]) => {
      delete this._dirtyParents[key];
      if (!parent.isDisposed()) parent.uid;
    });
  }
}
