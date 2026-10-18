// A target union of null/undefined and one type is that type normalized: an interface that adds nothing to its one base
// is that base (tsc's isRelatedTo: getNormalizedType(candidate)), so errors name `Omit<S, "element">`, not L (vitest)
// @strict: true
interface S { element?: Element; path?: string; save?: boolean }
interface L extends Omit<S, 'element'> {}
declare function shot2(options?: L): Promise<string>;
shot2({ save: true, path: "p", maskColor: 1 });
shot2(5);
shot2({ save: 1 });
declare const x: { save: number };
shot2(x);
let y: L | undefined = x;
let z: L = x;
