//// [test/cases/ttttPartialSpreadMethodIsProperty.ts] ////

//// [ttttPartialSpreadMethodIsProperty.ts]
// A method spread from one branch of a conditional is an optional property, not an optional method (tsc's
// tryMergeUnionOfObjectTypeAndEmptyObject; better-auth's get-id-field.ts)
declare const b: boolean;
export const f = () => ({ a: 1, ...(b ? { m() { return 1; }, n: "" } : {}) });
export const g = { ...(b ? { m(): string | undefined { return ""; } } : {}) };


//// [ttttPartialSpreadMethodIsProperty.js]
export const f = () => ({ a: 1, ...(b ? { m() { return 1; }, n: "" } : {}) });
export const g = { ...(b ? { m() { return ""; } } : {}) };


//// [ttttPartialSpreadMethodIsProperty.d.ts]
export declare const f: () => {
    a: number;
    m?: (() => number) | undefined;
    n?: string | undefined;
};
export declare const g: {
    m?: (() => string | undefined) | undefined;
};
