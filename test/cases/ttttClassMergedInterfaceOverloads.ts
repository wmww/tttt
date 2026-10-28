// @target: es2022
// A class's method merged with an interface's method signatures of its name is one symbol (tsc's
// checkFunctionOrConstructorSymbol): accessibility, abstractness and optionality agree across both (TS2385, TS2512,
// TS2386, the canonical one the first declaration unless the implementation shares its container), and the
// signatures are overloads the implementation must fit (TS2394)

interface I { m(): void }
class I { private m() {} }

class J { protected n() {} }
interface J { n(): void }

interface K { o?(): void }
class K { o() {} }

class L { p() {} }
interface L { p?(): void }

interface M { q(): void; q(x: number): void }
abstract class M { abstract q(): void }

declare class N { r(): void }
interface N { r(x: number): void }
interface N { r(x: string): void }
class O { private s() {} }
interface O { t(): void }

interface I2 { m(x: string): void }
class I2 { m(x: number) {} }
class J2 { n(x: number) {} }
interface J2 { n(x: string): void }
class K2 { o(): void; o(x?: number) {} }
interface K2 { o(x: string): void }
interface L2 { p(): void }
class L2 { private p(): void; private p() {} }

interface G<T> { m(x: T): void; m(x: T, y: number): void }
class G<T> { m(x: T, y?: number) {} }
interface H<T> { m(x: T): this }
class H<T> { m(x: T) { return this; } }
interface P2 { a?(): void; a(x: number): void }
class P2 { private b() {} }
class Q2 { m(x: string): void; m(x: any) {} }
interface Q2 { m(x: number): void }
interface R2 { m(x: number): string }
class R2 { m(x: number): number { return 1 } }
interface S2 { m(x: number): void }
declare class S2 { private m(): void }
