//// [test/cases/ttttHeritageOptionalChainDeclaration.ts] ////

//// [ttttHeritageOptionalChainDeclaration.ts]
// A heritage clause's `A?.B` is emitted as the parenthesized expression, each dot as written
export namespace Foo { export class Bar<T = 1> { x = 1 } export namespace N { export class Bar {} } }
export interface C1 extends Foo ?. Bar<number> {}
export interface C2 extends Foo.N?.Bar, Foo?.N.Bar {}
export class K implements Foo?.Bar<string> { x = 2 }


//// [ttttHeritageOptionalChainDeclaration.js]
// A heritage clause's `A?.B` is emitted as the parenthesized expression, each dot as written
export var Foo;
(function (Foo) {
    class Bar {
        x = 1;
    }
    Foo.Bar = Bar;
    let N;
    (function (N) {
        class Bar {
        }
        N.Bar = Bar;
    })(N = Foo.N || (Foo.N = {}));
})(Foo || (Foo = {}));
export class K {
    x = 2;
}


//// [ttttHeritageOptionalChainDeclaration.d.ts]
export declare namespace Foo {
    class Bar<T = 1> {
        x: number;
    }
    namespace N {
        class Bar {
        }
    }
}
export interface C1 extends (Foo?.Bar)<number> {
}
export interface C2 extends (Foo.N?.Bar), (Foo?.N.Bar) {
}
export declare class K implements (Foo?.Bar)<string> {
    x: number;
}
