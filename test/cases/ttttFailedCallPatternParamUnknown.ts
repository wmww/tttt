// After a call fails at an earlier argument, a later arrow function's destructured parameter is typed
// unknown by the candidate (T inferred from nothing), and its pattern reports TS2339 in tsc (mutation testing, arrayFrom);
// without the argument error the pattern's own type replaces unknown (assignParameterType: TS2345)
declare function h(f: (v: unknown) => void): void;
h(({ a }) => a);
declare function k<T>(x: T[], f: (v: T) => void): void;
k([], ({ a }) => a);
declare function m<T>(f: (v: T) => void): void;
m(({ a }) => a);
declare const v: void;
declare function g<T>(x: ArrayLike<T>, f: (v: T) => void): void;
g(v, ({ a }) => a);
