// @declaration: true
// @strict: true
// A generic function argument whose parameter is `f?: T` is instantiated in the context of T's constraint's signature:
// tsc's getApparentTypeOfContextualType (NoConstraints) leaves alone a bare type variable only, `T | undefined` is
// its members' apparent types (vitest's `vi.fn(<T extends Message>(type: T['type'], ...) => ...)`)
enum Bus { A = 'a', B = 'b' }
type Message = { type: Bus.A; x: number } | { type: Bus.B; y: string };
type Procedure = (...args: any[]) => any;
interface Mock<T extends Procedure = Procedure> { (...args: Parameters<T>): ReturnType<T>; mock: T }
declare function fn<T extends Procedure = Procedure>(f?: T): Mock<T>;
declare function fnRequired<T extends Procedure = Procedure>(f: T): Mock<T>;
declare function id<T extends Procedure>(f?: T): T;
export const optional = fn(<T extends Message>(type: T['type'], listener: (message: T) => void) => {});
export const required = fnRequired(<T extends Message>(type: T['type'], listener: (message: T) => void) => {});
export const any = id(<T extends string>(x: T, y: T[]) => x);
