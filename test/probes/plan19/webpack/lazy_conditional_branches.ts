// @noEmit: true
// tsc: clean (@jridgewell/trace-mapping's types.d.cts). A conditional type's branches are resolved when first needed
// (getTypeFromConditionalTypeNode resolves only the check and extends types): lowering `Readonly<RoArray<V>>` while
// declaring Ro forced RoArray's deferred `Ro<T>` element, a spurious TS2456
export type Ro<T> = T extends Array<infer V> ? V[] | Readonly<V[]> | RoArray<V> | Readonly<RoArray<V>> : T extends object ? T | Readonly<T> | RoObject<T> | Readonly<RoObject<T>> : T;
type RoArray<T> = Ro<T>[];
type RoObject<T> = {
    [K in keyof T]: T[K] | Ro<T[K]>;
};
export type Ro3<T> = T extends Array<infer V> ? Readonly<RoArray3<V>> : T;
type RoArray3<T> = Ro3<T>[];
// tsc: TS1254 on C and D only (isStringOrNumberLiteralExpression and isBigIntLiteralExpression take `-1`, `-1n`)
export declare const A = -1;
export declare const C = +1;
declare const D = -"x";
