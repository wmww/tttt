// A heritage clause's `A?.B` is emitted as the parenthesized expression, each dot as written
// @declaration: true
export namespace Foo { export class Bar<T = 1> { x = 1 } export namespace N { export class Bar {} } }
export interface C1 extends Foo ?. Bar<number> {}
export interface C2 extends Foo.N?.Bar, Foo?.N.Bar {}
export class K implements Foo?.Bar<string> { x = 2 }
