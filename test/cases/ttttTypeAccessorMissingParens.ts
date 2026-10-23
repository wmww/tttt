// An accessor in a type is committed after `get`/`set` and its name: without a parameter list it is TS1005 '('
// expected, and the annotation is the return type (tsc's parseAccessorDeclaration). Babel's
// interface/invalid-get-properties and invalid-set-properties fixtures
interface Foo {
  get foo: string;
  set bar: string;
}
type T = { get baz: number };
interface Ok { get: string; set?: number; get x(): string }
