// @target: es2021
// nestjs-i18n's `[a, b] = await Promise.all([i18n.t('x'), i18n.t('y')])`: the await operand is contextually typed
// (tsc's getContextualTypeForAwaitOperand), Promise.all's homomorphic mapped return reverse maps a tuple, and a
// generic mapped contextual type types array literals as tuples and gives their elements contextual types
declare function id<R = unknown>(): R;
declare function all3<T extends readonly unknown[] | []>(values: T): { -readonly [P in keyof T]: Awaited<T[P]> };
async function g() {
    let s: string | undefined, t: string | undefined;
    [s, t] = await Promise.all([id(), id()]);
    let w1: [string, string] = await Promise.all([id(), id()] as const);
    let w2: [string, string] = all3([id(), id()]);
    let w3: string = await id();
}
type Accessors<T> = { [K in keyof T]: (() => T[K]) | Computed<T[K]> };
type Computed<T> = { get?(): T; set?(value: T): void; }
declare function acc<P>(o: Accessors<P>): P;
const v1 = acc({ hello: { set(value: string) { } } });
