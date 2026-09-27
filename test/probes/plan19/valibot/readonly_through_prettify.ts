// @strict: true
type Prettify<T> = { [K in keyof T]: T[K] } & {};
type F = Prettify<{ readonly nested?: { a: 1 }; readonly other?: string }>;
declare const f: F;
f.nested = { a: 1 };
f.other = "x";
type G = { [K in keyof { readonly x: 1 }]: 1 };
declare const g: G;
g.x = 1;
function h(): F {
  const e: F = {};
  if (!e.nested) {
    e.nested = { a: 1 };
  }
  return e;
}
type FE<T> = Prettify<{ readonly nested?: Prettify<Readonly<Partial<Record<T extends string ? T : string, [string]>>>> }>;
function k(): FE<undefined> {
  const e: FE<undefined> = {};
  if (!e.nested) {
    e.nested = {};
  }
  e.nested["a"] = ["x"];
  return e;
}
