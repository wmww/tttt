// @target: es2015
// A heritage clause's optional chain name (`A?.B`): TS2499/TS2500, and resolved as the type reference it would be
// (TS2702, TS2503, TS2694; the interface extends it, its qualifiers have their values' types)
namespace Foo {
    export class Bar { x = 1 }
    export namespace N { export class Bar { y = "" } }
}
interface C1 extends Foo?.Bar {}
interface C2 extends Object?.Bar {}
interface C3 extends Nope?.Bar {}
interface C4 extends Foo?.Baz {}
interface C5 extends Foo?.Bar<number> {}
class K implements Foo?.Bar {}
class K2 implements Foo?.Baz, Foo?.#x {}
interface C6 extends Foo.Bar, Foo?.N.Bar {}
interface C7 extends Foo?.N.Bar {}
interface C8 extends Foo.N?.Bar {}
interface C9 extends Foo?.N?.Bar {}
class K3 implements Foo.N?.Bar {}
declare const c1: C1;
const s1: string = c1.x;
declare const c7: C7;
const n7: number = c7.y;
