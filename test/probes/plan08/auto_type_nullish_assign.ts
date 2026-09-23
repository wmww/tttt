// @strict: true
interface Ty { base?: Ty; flags: number }
declare function compute(t: Ty): Ty | undefined;
declare const circular: Ty;
declare const none: Ty;
declare function more(): boolean;
export function get(t: Ty): Ty {
    if (!t.base) {
        let result;
        if (more()) {
            result = compute(t);
        }
        if (more()) {
            result = circular;
        }
        t.base ??= result || none;
    }
    return t.base;
}
