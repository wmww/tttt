// @target: es2022
// @module: esnext
// `export as namespace` makes no module (tsc's isAnExternalModuleIndicatorNode), so a top-level `await` stays an
// identifier: the parser's module test is the program's
export as namespace N;
declare function await(x: number): number;
const y: 1 = await(1);
