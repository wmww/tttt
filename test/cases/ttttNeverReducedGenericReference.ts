// @strict: true
// tsc's getReducedType: a discriminant conflict with a generic instantiation is never (one literal side is enough,
// CheckFlags.HasLiteralPart): Object.assign({ onError: null, ... }, opts) with opts: AwaiterOpts<T | null> (vencord)
interface AwaiterOpts<T> {
    fallbackValue: T;
    deps?: unknown[];
    onError?(e: any): void;
    onSuccess?(value: T): void;
}
export function useAwaiter<T>(providedOpts?: AwaiterOpts<T | null>) {
    const opts: Required<AwaiterOpts<T | null>> = Object.assign({
        fallbackValue: null,
        deps: [],
        onError: null,
    }, providedOpts);
    return opts;
}
function f<T>() {
    type X6 = { onError: null } & AwaiterOpts<T | null>;
    let x6: X6 = null!;
    type X7 = { w: null; onError: null } & AwaiterOpts<T | null>;
    let x7: X7 = null!;
    type X8 = { onError: null; w: null } & AwaiterOpts<T | null>;
    let x8: X8 = null!;
    return [x6, x7, x8];
}
