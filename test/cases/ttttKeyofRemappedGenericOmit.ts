// @strict: true
// keyof a generic mapped type with an `as` clause over `keyof X` includes the `as` type of X's apparent keys, a
// generic mapped part of X included: `Omit<E[T], K>` has the keys every E[T] has (tsc's getApparentMappedTypeKeys over
// getPropertiesOfType). solid-js's splitProps of what an earlier splitProps left (ark's factory)
type SplitProps<T, K extends (readonly (keyof T)[])[]> = [
    ...{ [P in keyof K]: P extends `${number}` ? Pick<T, Extract<K[P], readonly (keyof T)[]>[number]> : never },
    { [P in keyof T as Exclude<P, K[number][number]>]: T[P] }
];
declare function splitProps<T extends Record<any, any>, K extends [readonly (keyof T)[], ...(readonly (keyof T)[])[]]>(props: T, ...keys: K): SplitProps<T, K>;
interface Elements { div: { ref?: HTMLDivElement; id?: string }; span: { ref?: HTMLSpanElement; title?: string } }
type ElementType = keyof Elements;
type Assign<T, U> = Omit<T, keyof U> & U;
type Props<E extends ElementType> = Assign<Elements[E], { asChild?: () => void }>;
export function f<T extends ElementType>(props: Props<T>) {
    const [local, parent] = splitProps(props, ['asChild']);
    const [, rest] = splitProps(parent, ['ref']);
    return [local, rest];
}
