// nestjs ConfigService.getOrThrow('app.corsOrigins', { infer: true }): P's constraint Path<T> has a base constraint
// with string in a union, so the literal argument stays (tsc's isLiteralOfContextualType, maybeTypeOfKind)
type IsAny<T> = unknown extends T ? [keyof T] extends [never] ? false : true : false;
type PathImpl<T, Key extends keyof T> = Key extends string ? IsAny<T[Key]> extends true ? never : T[Key] extends Record<string, any> ? `${Key}.${PathImpl<T[Key], Exclude<keyof T[Key], keyof any[]>> & string}` | `${Key}.${Exclude<keyof T[Key], keyof any[]> & string}` : never : never;
type PathImpl2<T> = PathImpl<T, keyof T> | keyof T;
type Path<T> = keyof T extends string ? PathImpl2<T> extends infer P ? P extends string | keyof T ? P : keyof T : keyof T : never;
type PathValue<T, P extends Path<T>> = P extends `${infer Key}.${infer Rest}` ? Key extends keyof T ? Rest extends Path<T[Key]> ? PathValue<T[Key], Rest> : never : never : P extends keyof T ? T[P] : never;
type Cfg = { app: { cors: string[]; n: number } };
declare function h<T = Cfg, P extends Path<T> = any, R = PathValue<T, P>>(p: P): R;
const v1 = h("app.cors");
const e1: { x: 1 } = v1;
type C<T, P> = P extends string | keyof T ? P : keyof T;
declare function c<T = {x: 1, y: 2}, Q = 1, P extends C<T, Q> = any>(p: P): { p: P };
const v2 = c("x");
