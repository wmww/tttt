// @strict: true
// setupTests.tsx: `window.scrollTo = vi.fn()`, vitest's Mock<T> ends in `& { [P in keyof T]: T[P] }`. A source without
// properties or string index signature (here an overloaded function type) isn't reverse mappable (tsc's
// createReverseMappedType), so the return type infers nothing for T and it is its default
type Procedure = (...args: any[]) => any;
type M1<T extends Procedure> = { [P in keyof T]: T[P] };
type M2<T extends Procedure> = { x: 1 } & { [P in keyof T]: T[P] };
declare function f1<T extends Procedure = Procedure>(o?: T): M1<T>;
declare function f2<T extends Procedure = Procedure>(o?: T): M2<T>;
interface O { a: number }
declare var w: { scrollTo(options?: O): void; scrollTo(x: number, y: number): void; }
w.scrollTo = f1()
w.scrollTo = f2()
declare function f3<T>(o?: T): { [P in keyof T]: T[P] };
const withProps: { (): void; p: string } = f3()
