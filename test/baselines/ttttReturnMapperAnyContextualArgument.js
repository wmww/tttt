//// [test/cases/ttttReturnMapperAnyContextualArgument.ts] ////

//// [ttttReturnMapperAnyContextualArgument.ts]
// A literal argument's contextual type is the parameter type instantiated with the return mapper unless that is any or
// unknown (tsc's instantiateContextualType): S -> any from `Struct<any, any>` leaves S, whose constraint types `x`
// (snaps-cli's superstruct config, `builtIns?: any`)
declare class Struct<T, S> { t: T; s: S; }
declare function defaulted<T, S>(struct: Struct<T, S>, fallback: any): Struct<T, S>;
declare function object<S extends Record<string, Struct<any, any>>>(s: S): Struct<S, S>;
declare const anyv: any;
export const a: Struct<any, any> = object({ x: defaulted(anyv, {}) });
export const b = object({ y: object({ x: defaulted(anyv, {}) }) });
declare function box<T>(x: T): { value: T };
export const c: { value: any } = box({ n: 1 });
export const d: { value: unknown } = box([1, "a"]);


//// [ttttReturnMapperAnyContextualArgument.js]
export const a = object({ x: defaulted(anyv, {}) });
export const b = object({ y: object({ x: defaulted(anyv, {}) }) });
export const c = box({ n: 1 });
export const d = box([1, "a"]);


//// [ttttReturnMapperAnyContextualArgument.d.ts]
declare class Struct<T, S> {
    t: T;
    s: S;
}
export declare const a: Struct<any, any>;
export declare const b: Struct<{
    y: Struct<{
        x: Struct<any, any>;
    }, {
        x: Struct<any, any>;
    }>;
}, {
    y: Struct<{
        x: Struct<any, any>;
    }, {
        x: Struct<any, any>;
    }>;
}>;
export declare const c: {
    value: any;
};
export declare const d: {
    value: unknown;
};
export {};
