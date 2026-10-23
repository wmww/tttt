// @strict: true
// @target: es2023
// @lib: es2023,dom
// A void contextual return type is a contextual type for the returned call's return type inference too (tsc's
// getContextualReturnType keeps void): U's candidate void fails `U extends Set<string>`, so U is Set<string> where
// the argument is contextually typed and `new Set()` infers string. openclaw's solid `onClick={() => setIds(new Set())}`
export type Setter<in out T> = {
    <U extends T>(...args: undefined extends T ? [] : [value: Exclude<U, Function> | ((prev: T) => U)]): undefined extends T ? undefined : U;
    <U extends T>(value: (prev: T) => U): U;
    <U extends T>(value: Exclude<U, Function>): U;
    <U extends T>(value: Exclude<U, Function> | ((prev: T) => U)): U;
};
declare const setIds: Setter<Set<string>>;
const h1: () => void = () => setIds(new Set());
const h2: (e: MouseEvent) => any = () => setIds(new Set());
declare function btn(p: { onClick?: (e: MouseEvent) => void }): void;
btn({ onClick: () => setIds(new Set()) });
btn({ onClick: () => { return setIds(new Set()); } });
