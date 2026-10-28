// @allowJs: true
// @checkJs: true
// @noEmit: true
// @strict: true
// An `@augments`/`@implements` class is no type (tsc's parseExpressionWithTypeArgumentsForAugments): the `<` right
// after its name is the JSDoc scanner's one-character token, so `A<=` has an empty type argument list (TS1099, '>'
// expected) and its `=` is no optional type, braces or not; an `@implements` class missing its type arguments asks for
// an `@extends` tag (TS8026). Elsewhere `<=` and `<<=` start no type argument list (only a `<<` is rescanned), a
// `<<`/`<=` token starts no type argument, and type parameters in a type commit a function type: `{<>}` is '('
// expected
// (The class's type parameter has a default: tsc's TS2314 underlines `A<`, the node with its missing `>`, tttt's
// `A`, identifier_type_span())
// @filename: a.js
/** @template [T=number] */
class A { /** @type {T | undefined} */ x = undefined; }
/** @template T */
class G {}
/** @augments {A<=} */
class B extends A {}
/** @augments {A<<=} */
class C extends A {}
/** @augments A<= */
class D extends A {}
/** @implements {A<=} */
class E { x = undefined }
/** @implements {G} */
class F { }
/** @type {<>} */
let a;
/** @type {Array<<>>} */
let b;
/** @type {Array<<>() => void>} */
let c;
// @filename: b.ts
let d: Array<<T>() => T>;
let e: <>;
let f: Array<<>>;
