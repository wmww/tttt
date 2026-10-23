//// [test/cases/ttttSpreadIntersectionAfterGeneric.ts] ////

//// [ttttSpreadIntersectionAfterGeneric.ts]
// A non-generic intersection spread after a generic object is intersected as it is (tsc's getSpreadType:
// getIntersectionType([left, right]) once left is generic; an intersection is no object type the left's last member
// merges with): unovis's `{ ...preservedProps, ...useAttrs() }` is `T & Data & AllowedAttrs`
type Data = Record<string, unknown>;
interface Allowed { class?: string }
type Attrs = Data & Allowed;
declare const attrs: Attrs;
export function f<T extends Record<string, any>>(props: T) {
    return { ...props, ...attrs };
}
export function g<T extends Record<string, any>>(props: T) {
    return { ...props, ...attrs, x: 1 };
}
export function h<T extends Record<string, any>>(props: T) {
    return { ...props, ...attrs, ...attrs };
}


//// [ttttSpreadIntersectionAfterGeneric.js]
export function f(props) {
    return { ...props, ...attrs };
}
export function g(props) {
    return { ...props, ...attrs, x: 1 };
}
export function h(props) {
    return { ...props, ...attrs, ...attrs };
}


//// [ttttSpreadIntersectionAfterGeneric.d.ts]
type Data = Record<string, unknown>;
interface Allowed {
    class?: string;
}
export declare function f<T extends Record<string, any>>(props: T): T & Data & Allowed;
export declare function g<T extends Record<string, any>>(props: T): T & Data & {
    class?: string;
    x: number;
};
export declare function h<T extends Record<string, any>>(props: T): T & Data & Allowed;
export {};
