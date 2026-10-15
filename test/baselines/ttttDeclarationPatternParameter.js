//// [test/cases/ttttDeclarationPatternParameter.ts] ////

//// [ttttDeclarationPatternParameter.ts]
// A binding pattern parameter with an initializer, of a function whose return type is written: its type is the
// signature's parameter's (resolved: the written return type didn't need it)
export function notFound({ verbose = false } = {}): void {}
export function g({ a, b = 1 }: { a: string; b?: number } = { a: "" }): void {}
export function h([x, y] = [1, 2]): void {}
export function k({ a: { b } } = { a: { b: 1 } }): void {}
export const arrow = ({ verbose = false } = {}) => verbose;
export class C { m({ v = 1 } = {}) {} }


//// [ttttDeclarationPatternParameter.js]
// A binding pattern parameter with an initializer, of a function whose return type is written: its type is the
// signature's parameter's (resolved: the written return type didn't need it)
export function notFound({ verbose = false } = {}) { }
export function g({ a, b = 1 } = { a: "" }) { }
export function h([x, y] = [1, 2]) { }
export function k({ a: { b } } = { a: { b: 1 } }) { }
export const arrow = ({ verbose = false } = {}) => verbose;
export class C {
    m({ v = 1 } = {}) { }
}


//// [ttttDeclarationPatternParameter.d.ts]
export declare function notFound({ verbose }?: {
    verbose?: boolean | undefined;
}): void;
export declare function g({ a, b }?: {
    a: string;
    b?: number;
}): void;
export declare function h([x, y]?: [number, number]): void;
export declare function k({ a: { b } }?: {
    a: {
        b: number;
    };
}): void;
export declare const arrow: ({ verbose }?: {
    verbose?: boolean | undefined;
}) => boolean;
export declare class C {
    m({ v }?: {
        v?: number | undefined;
    }): void;
}
