// @strict: true
// @target: es2022
// A comma expression as an object literal's computed key is TS1171 from the checker's
// checkGrammarObjectLiteralExpression (bun 28-C12), which a destructuring assignment's target never reaches
declare function g(): any;
let a: any;
({ [g(), "a"]: a } = {} as any);
for ({ [g(), "d"]: a } of [] as any[]) {}
const o = { [g(), "b"]: 1, [(g(), "c")]: 2 };
