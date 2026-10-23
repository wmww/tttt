//// [test/cases/ttttTypeofFunctionExpressionInFakeScope.ts] ////

//// [ttttTypeofFunctionExpressionInFakeScope.ts]
// A function expression of a top-level variable, met again inside its own inferred type, is `typeof v` where tsgo's
// enterNewScope made a fake scope the enclosing declaration (a signature with parameters or type parameters on the
// way, or a mapped type): only directly in the variable's own declaration is it the cyclic structure (TS5088).
// opencode's packages/ai: `export const configure = (input: Config) => { ...; return { ..., configure } }`
export const configure = (input: string) => {
    const chat = (id: string) => id + input;
    return { chat, configure };
};
export var x = function somefn(a: number) { return somefn; };
export const y = () => ({ f: (q: number) => y });
export const w = <T,>() => [w];
export const b1 = () => { return { b1 }; };
export const d5 = function () { return { d5 }; };


//// [ttttTypeofFunctionExpressionInFakeScope.js]
// A function expression of a top-level variable, met again inside its own inferred type, is `typeof v` where tsgo's
// enterNewScope made a fake scope the enclosing declaration (a signature with parameters or type parameters on the
// way, or a mapped type): only directly in the variable's own declaration is it the cyclic structure (TS5088).
// opencode's packages/ai: `export const configure = (input: Config) => { ...; return { ..., configure } }`
export const configure = (input) => {
    const chat = (id) => id + input;
    return { chat, configure };
};
export var x = function somefn(a) { return somefn; };
export const y = () => ({ f: (q) => y });
export const w = () => [w];
export const b1 = () => { return { b1 }; };
export const d5 = function () { return { d5 }; };
