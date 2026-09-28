// @experimentalDecorators: true
// @target: es2015
declare function factory<T>(value: T): (target: object, key: string | symbol) => void;
declare function over(target: Function): void;
declare function over(target: object, key: string, descriptor: PropertyDescriptor): void;
declare const either: ((target: object, key: string) => void) | ((target: object, key: string, d: number) => void);
declare function param(target: object, key: string | symbol | undefined, index: number): void;
declare function badParam(target: object, key: undefined, index: 1): void;
declare function wrongReturn(target: Function): number;
declare function methodOnly(target: object, key: string, d: TypedPropertyDescriptor<() => void>): TypedPropertyDescriptor<() => void>;
const sym = Symbol();

@over
@wrongReturn
class C {
    constructor(@param a: number, @badParam b: string) {}
    @factory(1) prop: number = 1;
    @over method(@param x: number, @param y: string) {}
    @either other() {}
    @methodOnly [sym]() {}
    @methodOnly ["computed"](x: number) {}
    @factory("s") static s: string;
    m2(this: C, @param z: number) {}
}
