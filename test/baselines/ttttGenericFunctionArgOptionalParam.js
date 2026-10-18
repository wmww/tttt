//// [test/cases/ttttGenericFunctionArgOptionalParam.ts] ////

//// [ttttGenericFunctionArgOptionalParam.ts]
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


//// [ttttGenericFunctionArgOptionalParam.js]
// A generic function argument whose parameter is `f?: T` is instantiated in the context of T's constraint's signature:
// tsc's getApparentTypeOfContextualType (NoConstraints) leaves alone a bare type variable only, `T | undefined` is
// its members' apparent types (vitest's `vi.fn(<T extends Message>(type: T['type'], ...) => ...)`)
var Bus;
(function (Bus) {
    Bus["A"] = "a";
    Bus["B"] = "b";
})(Bus || (Bus = {}));
export const optional = fn((type, listener) => { });
export const required = fnRequired((type, listener) => { });
export const any = id((x, y) => x);


//// [ttttGenericFunctionArgOptionalParam.d.ts]
declare enum Bus {
    A = "a",
    B = "b"
}
type Message = {
    type: Bus.A;
    x: number;
} | {
    type: Bus.B;
    y: string;
};
type Procedure = (...args: any[]) => any;
interface Mock<T extends Procedure = Procedure> {
    (...args: Parameters<T>): ReturnType<T>;
    mock: T;
}
export declare const optional: Mock<(type: Bus, listener: (message: Message) => void) => void>;
export declare const required: Mock<<T extends Message>(type: T['type'], listener: (message: T) => void) => void>;
export declare const any: (x: any, y: any[]) => any;
export {};
