// An object type with known keys and a generic property against a generic mapped type falls back to nothing (tsc's
// structuredTypeRelatedToWorker: False unless the mapped rules relate it): not related through the members of the
// constraint's constraint, whatever the mapped type iterates over (keyof X, keyof X & {...}, a deferred conditional's keys)
export function g6<T extends 'email', X extends E>(s: { type: T }) {
    const x: Partial<X> = s;
}
export function g7<T extends 'email', X extends E>(s: { type: T }) {
    const x: Partial<X & { a: 1 }> = s;
}
export function g8<T extends 'email', X extends E>(s: { type: T }) {
    const x: Readonly<CT<T>> = s;
}
export function g9<T extends 'email', X extends E>(s: { type: T }) {
    const x: Partial<X["type"] extends T ? E : never> = s;
}
interface E { id: string; type: 'email'; addres: string; }
type CT<T, A = E> = A extends { type: T } ? A : never;
type NC2<T extends E> = Pick<T, 'type'> & Partial<Pick<T, 'addres'>> & { localChannelId: string };
export function f2<T extends 'email'>(type: T): NC2<CT<T>> {
    return { type, localChannelId: "" };
}
type NC3<T extends E> = Pick<T, 'type'> & Partial<T> & { localChannelId: string };
export function f3<T extends 'email'>(type: T): NC3<CT<T>> {
    return { type, localChannelId: "" };
}
export function f4<T extends 'email'>(type: T) {
    const x: Partial<CT<T>> & { localChannelId: string } = { type, localChannelId: "" };
}
