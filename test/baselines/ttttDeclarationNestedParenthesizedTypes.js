//// [test/cases/ttttDeclarationNestedParenthesizedTypes.ts] ////

//// [ttttDeclarationNestedParenthesizedTypes.ts]
// Nested parentheses around a type, `((C<Step>))`, are each a node tsgo reuses: declaration emit writes them all and
// maps each (effect's ExecutionPlan StepProvides; tttt kept one `parenthesized` flag)
type C<T> = T extends { p: infer P } ? P : unknown;
export type S<Steps, Out = unknown> = Steps extends [infer Step, ...infer Rest] ? S<Rest, Out & ((C<Step>))> : Out;
export declare const x: ((string)) | (((number[])));
export declare function f(a: ((() => void)) | null): void;


//// [ttttDeclarationNestedParenthesizedTypes.js]
export {};


//// [ttttDeclarationNestedParenthesizedTypes.d.ts]
type C<T> = T extends {
    p: infer P;
} ? P : unknown;
export type S<Steps, Out = unknown> = Steps extends [infer Step, ...infer Rest] ? S<Rest, Out & ((C<Step>))> : Out;
export declare const x: ((string)) | (((number[])));
export declare function f(a: ((() => void)) | null): void;
export {};
//# sourceMappingURL=ttttDeclarationNestedParenthesizedTypes.d.ts.map