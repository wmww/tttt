// @strict: true
// A `void` return type is a contextual type for the returned expression like any other (tsc's
// getContextualTypeForReturnExpression): a generic call there infers `void` for a type parameter only its return
// mentions
declare function finish<T>(userId: string): T;
export function a(): void { return finish("u"); }
export function b(): number { return finish("u"); }
export const c = (): void => finish("u");
export const d: () => void = () => finish("u");
export function e(): void { return [1].map(x => x) as any; }
declare function id<T>(x: T): T;
export function f(): void { return id(undefined); }
