// @strict: true
type F = (...a: string[]) => string;
type A = F extends Record<PropertyKey, unknown> ? 1 : 2;
type B = { (): void } extends Record<string, unknown> ? 1 : 2;
type C = { new (): void; x: 1 } extends Record<string, unknown> ? 1 : 2;
declare const a: A;
declare const b: B;
declare const c: C;
