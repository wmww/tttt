//// [test/cases/ttttDeclarationKeyofFallbackConstituentParens.ts] ////

//// [ttttDeclarationKeyofFallbackConstituentParens.ts]
// A reused `keyof T & string` whose T is mapped to an object type prints `keyof T`'s type, the union of the keys, in
// its place: parenthesized as an intersection member like any fallback of a constituent node (slate-hyperscript's jsx
// printed `S extends "anchor" | ... | "text" & string`)
const createFactory = <T extends Record<string, (...a: any[]) => any>>(creators: T) => {
  const jsx = <S extends keyof T & string>(tagName: S): ReturnType<T[S]> => creators[tagName]();
  return jsx;
};
export const jsx = createFactory({ anchor: () => 1, text: () => 'x' });
type K = keyof { a: 1; b: 2 };
export declare function g<S extends K & string>(s: S): S;
export const g2 = g;


//// [ttttDeclarationKeyofFallbackConstituentParens.js]
// A reused `keyof T & string` whose T is mapped to an object type prints `keyof T`'s type, the union of the keys, in
// its place: parenthesized as an intersection member like any fallback of a constituent node (slate-hyperscript's jsx
// printed `S extends "anchor" | ... | "text" & string`)
const createFactory = (creators) => {
    const jsx = (tagName) => creators[tagName]();
    return jsx;
};
export const jsx = createFactory({ anchor: () => 1, text: () => 'x' });
export const g2 = g;


//// [ttttDeclarationKeyofFallbackConstituentParens.d.ts]
export declare const jsx: <S extends ("anchor" | "text") & string>(tagName: S) => ReturnType<{
    anchor: () => number;
    text: () => string;
}[S]>;
type K = keyof {
    a: 1;
    b: 2;
};
export declare function g<S extends K & string>(s: S): S;
export declare const g2: typeof g;
export {};
