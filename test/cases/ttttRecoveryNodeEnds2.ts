// @strict: true
// Under recovery a node ends with its last token, not the trivia after it: a binding pattern's computed name
// without its `]` (TS2464), an object literal without its `}` before a comment (its types text), a signature
// without its `)` at the end of the file (TS7013). Seed 20261116 --syntax: excessPropertyChecksWithNestedIntersections,
// typeSatisfaction_contextualTyping2, declarationEmitTypeParameterNameReusedInOverloads delete-stmt
let x = {
  a: 1
export type View<T> = { [K in keyof T]: 1 };
let obj: { f(s: string): void } & Record<string, unknown> = {
    f(s) { },
    g(s) { }

// a comment
({ f(x) { } }) satisfies { f(s: string): void };

class Base { foo!: string; }
class Derived extends Base { bar!: string; }
class Derived2 extends Derived { baz!: string; }
export type Foo = {
    new (x: {
        new <T extends Derived>(a: T): T;
        new <T extends Base>(a: T): T;
    new (x: {
        new <T extends Derived2>(a: T): T;
            new <T extends Base>(a: T): T;
    }): any[];
}
