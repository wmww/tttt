// @strict: true
// A union's signatures where one's rest parameter is a union with a non-tuple (`[string] | undefined`, TS2370): its
// parameter types are the rest indexed like tsc's getIndexedAccessType (no property in undefined: no type), so it
// matches `(...args: [number])` and the union's signature is that one's (TS2556 for a spread, TS2554, number)
declare const args: any;
declare const f1: ((...args: []) => unknown) | ((...args: [string] | undefined) => unknown);
f1(...args);
declare const f2: ((...args: [string] | undefined) => unknown);
f2(...args);
declare const f3: ((...args: [number]) => unknown) | ((...args: [string] | undefined) => unknown);
f3(...args);
f3();
f3(1, 2, 3);
f3("a");
