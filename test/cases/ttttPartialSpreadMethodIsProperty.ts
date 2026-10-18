// @strict: true
// @declaration: true
// A method spread from one branch of a conditional is an optional property, not an optional method (tsc's
// tryMergeUnionOfObjectTypeAndEmptyObject; better-auth's get-id-field.ts)
declare const b: boolean;
export const f = () => ({ a: 1, ...(b ? { m() { return 1; }, n: "" } : {}) });
export const g = { ...(b ? { m(): string | undefined { return ""; } } : {}) };
