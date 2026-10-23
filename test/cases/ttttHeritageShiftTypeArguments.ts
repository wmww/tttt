// A heritage element's type arguments want a `<` token (tsc's parseExpressionWithTypeArguments, tryParseTypeArguments):
// `extends f<<T>(v: T) => void>` has a `<<`, which only an expression's parseTypeArgumentsInExpression rescans, so the
// clause is `f` and the rest is recovery. Babel's type-arguments-bit-shift-left-like/class-heritage fixture
declare const f: new () => {};
(class extends f<<T>(v: T) => void> {});
