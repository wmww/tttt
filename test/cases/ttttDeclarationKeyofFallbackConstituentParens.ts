// A reused `keyof T & string` whose T is mapped to an object type prints `keyof T`'s type, the union of the keys, in
// its place: parenthesized as an intersection member like any fallback of a constituent node (slate-hyperscript's jsx
// printed `S extends "anchor" | ... | "text" & string`)
// @strict: true
// @declaration: true
const createFactory = <T extends Record<string, (...a: any[]) => any>>(creators: T) => {
  const jsx = <S extends keyof T & string>(tagName: S): ReturnType<T[S]> => creators[tagName]();
  return jsx;
};
export const jsx = createFactory({ anchor: () => 1, text: () => 'x' });
type K = keyof { a: 1; b: 2 };
export declare function g<S extends K & string>(s: S): S;
export const g2 = g;
