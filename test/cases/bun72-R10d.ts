// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class Base<T> { b!: T }
type P1<T> = T extends string ? 1 : 2;
type P2<T> = T extends string | number ? 1 : 2;
type P3<T> = T extends null | undefined ? 1 : 2;
type P4<T> = T extends object ? 1 : 2;
type P5<T> = T extends Function ? 1 : 2;
type P6<T> = T extends any[] ? 1 : 2;
type P7<T> = T extends string | any[] ? 1 : 2;
type P8<T> = T extends "a" | "b" ? 1 : 2;
type P9<T> = T extends boolean ? 1 : 2;
type P10<T> = T extends string | object ? 1 : 2;
type P11<T> = T extends unknown ? 1 : 2;
type P12<T> = T extends never ? 1 : 2;
type P13<T> = T extends {} ? 1 : 2;
type P14<T> = [T] extends [string | number] ? 1 : 2;
type P15<T> = T extends string & { brand: 1 } ? 1 : 2;
type P16<T> = T extends `a${string}` | number ? 1 : 2;
type P17<T> = T extends keyof any ? 1 : 2;
type P18<T> = string | number extends T ? 1 : 2;
type P19<T> = T extends bigint | symbol ? T : { readonly [K in keyof T]: P19<T[K]> };
class D1 extends Base<P1<D1>> { x = 1 }
class D2 extends Base<P2<D2>> { x = 1 }
class D3 extends Base<P3<D3>> { x = 1 }
class D4 extends Base<P4<D4>> { x = 1 }
class D5 extends Base<P5<D5>> { x = 1 }
class D6 extends Base<P6<D6>> { x = 1 }
class D7 extends Base<P7<D7>> { x = 1 }
class D8 extends Base<P8<D8>> { x = 1 }
class D9 extends Base<P9<D9>> { x = 1 }
class D10 extends Base<P10<D10>> { x = 1 }
class D11 extends Base<P11<D11>> { x = 1 }
class D12 extends Base<P12<D12>> { x = 1 }
class D13 extends Base<P13<D13>> { x = 1 }
class D14 extends Base<P14<D14>> { x = 1 }
class D15 extends Base<P15<D15>> { x = 1 }
class D16 extends Base<P16<D16>> { x = 1 }
class D17 extends Base<P17<D17>> { x = 1 }
class D18 extends Base<P18<D18>> { x = 1 }
class D19 extends Base<P19<D19>> { x = 1 }
class D20 extends Base<D20 extends string | number ? 1 : 2> { x = 1 }
class D21 extends Base<Extract<D21, string | number>> { x = 1 }
class D22 extends Base<Exclude<D22, string | number>> { x = 1 }
class D23 extends Base<NonNullable<D23>> { x = 1 }
class D24 extends Base<Awaited<D24>> { x = 1 }
namespace NS { export class B<T> { b!: T } }
class D25 extends NS.B<P2<D25>> { x = 1 }
class D26 extends (Base)<P2<D26>> { x = 1 }
class D27 extends Base<P2<D27>> {}
class D28 extends Base<P2<D28>> { static s = 1 }
abstract class D29 extends Base<P2<D29>> { abstract x: number }
const D30 = class D30i extends Base<P2<D30i>> { x = 1 };
class D31<T> extends Base<P2<D31<T>>> { x!: T }
class D32 extends Base<Base<P2<D32>>> { x = 1 }
class D33 extends Base<{ p: P2<D33> }> { x = 1 }
class D34 extends Base<[P2<D34>]> { x = 1 }
class D35 extends Base<() => P2<D35>> { x = 1 }
class D36 extends Base<P2<D37>> { x = 1 } class D37 extends Base<P2<D36>> { y = 1 }
const sentinel: number = "";
