// @module: esnext
// @target: es2022
// Any `await` identifier makes tsc reparse its top-level statement in an await context (newIdentifier sets
// statementHasAwaitIdentifier: a type parameter's name, a type reference's too, inside a function as well), unless a
// speculative parse that fails took it (lookAhead and tryParse restore the flag). In the await context `<await>` and
// `<T, await>` start no arrow function (isIdentifier), and a function's type parameters are in the context around it
// @filename: a.ts
export {};
const q = <await>(x: await) => x;
// @filename: b.ts
export {};
const q = <T, await>(x: T) => x;
// @filename: c.ts
async function f() { const q = <T, await>(x: T) => x; }
// @filename: d.ts
export {};
declare const x: any;
({ await, a: await 1 });
