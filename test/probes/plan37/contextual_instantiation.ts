// @strict: true
// Contextual types during inference are instantiated leaf by leaf in tsc (instantiateContextualType only touches
// instantiable types; non-Signature leaves only with the returnMapper), and a function's return type is fixed at its first
// contextual check. So `a: true` widens against raw T there (arrow returns `{ a: boolean }`), while the final check sees
// `boolean` and prints the literal `{ a: true }`. tttt instantiates the whole parameter type with the so-far mapper: FAILS
declare function f<T>(g: () => { a: T, b: (x: T) => void }): T;
const s1 = f(() => ({ a: true, b: x => x }));
declare function k<T>(g: () => () => { a: T, b: (x: T) => void }): T;
const s2 = k(() => () => ({ a: true, b: (x: boolean) => x }));
const s3 = k(() => () => ({ a: true, b: x => x }));
declare function k2<T>(g: () => () => { a: T }): T;
const s4 = k2(() => () => ({ a: true }));
declare function k3<T>(g: (y: number) => () => { a: T, b: (x: T) => void }): T;
const s5 = k3(y => () => ({ a: true, b: x => x }));
