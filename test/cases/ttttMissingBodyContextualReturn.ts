// A function expression whose `{` is missing has an empty, missing block: a signature's return type is any then
// (getReturnTypeOfSignature checks NodeIsMissing), but its contextual check infers it from the block
// (contextuallyCheckFunctionExpressionOrObjectLiteralMethod -> getReturnTypeFromBody), whose end no flow reaches
// (the binder sets endFlowNode only for a present body): never. Seed 20261114, contextualTyping null-arg
var f = <(n: number, s: string) => string>(function(a, s) );
declare function g(cb: (x: number) => string): void;
g(function(x) );
let c: (x: number) => void = function(x);
let a1: (x: number) => Promise<string> = async function(x);
let g1: () => Iterator<number> = function*();
declare function inf<T>(cb: (x: number) => T): T;
let r = inf(function(x));
// Without a contextual signature: any
var q = function(x: number);
var o = { m(x: number) };
