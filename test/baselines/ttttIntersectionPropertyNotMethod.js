//// [test/cases/ttttIntersectionPropertyNotMethod.ts] ////

//// [ttttIntersectionPropertyNotMethod.ts]
// A property several parts of an intersection have is a Property (tsc's createUnionOrIntersectionProperty), not a
// method, also as a spread copies it: printed with its intersected type
declare class D { render(...props: unknown[]): unknown; update(): void; }
export const f = (c: new () => D & { render(): unknown }) => class extends c {};
export declare const x: D & { render(): unknown };
export const y = { ...x };
type I = { m(): void } & { m(x: number): void; n(): void };
declare const i: I;
export const z = { ...i };


//// [ttttIntersectionPropertyNotMethod.js]
export const f = (c) => class extends c {
};
export const y = { ...x };
export const z = { ...i };


//// [ttttIntersectionPropertyNotMethod.d.ts]
declare class D {
    render(...props: unknown[]): unknown;
    update(): void;
}
export declare const f: (c: new () => D & {
    render(): unknown;
}) => {
    new (): {
        render: ((...props: unknown[]) => unknown) & (() => unknown);
        update(): void;
    };
};
export declare const x: D & {
    render(): unknown;
};
export declare const y: {
    render: ((...props: unknown[]) => unknown) & (() => unknown);
};
export declare const z: {
    m: (() => void) & ((x: number) => void);
    n(): void;
};
export {};
