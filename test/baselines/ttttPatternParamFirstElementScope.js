//// [test/cases/ttttPatternParamFirstElementScope.ts] ////

//// [ttttPatternParamFirstElementScope.ts]
// tsgo quirk: where a signature is printed, only the first element of each parameter binding pattern is in its fake
// scope (enterNewScope's pattern loop returns after one), so `typeof b` of `({ a, b }: T)` isn't reused there
type Cfg = { app: number; animationState?: { a: 1 }; arr: [string, number] };
export const f = ({ app, animationState }: Cfg): { s?: typeof animationState; t: typeof app } => null!;
export const g = ([x, y]: [number, string]): { a: typeof x; b: typeof y } => null!;
export const h = ({ arr: [p, q] }: Cfg): { a: typeof p; b: typeof q } => null!;
export const k = ([, y]: [number, string]): { b: typeof y } => null!;


//// [ttttPatternParamFirstElementScope.js]
export const f = ({ app, animationState }) => null;
export const g = ([x, y]) => null;
export const h = ({ arr: [p, q] }) => null;
export const k = ([, y]) => null;


//// [ttttPatternParamFirstElementScope.d.ts]
type Cfg = {
    app: number;
    animationState?: {
        a: 1;
    };
    arr: [string, number];
};
export declare const f: ({ app, animationState }: Cfg) => {
    s?: {
        a: 1;
    } | undefined;
    t: typeof app;
};
export declare const g: ([x, y]: [number, string]) => {
    a: typeof x;
    b: string;
};
export declare const h: ({ arr: [p, q] }: Cfg) => {
    a: typeof p;
    b: number;
};
export declare const k: ([, y]: [number, string]) => {
    b: string;
};
export {};
