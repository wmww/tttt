// After type parameters an arrow function is one only if `=>` or `{` follows its signature (tsc's
// isParenthesizedArrowFunctionExpression is Unknown after `<`): a typed parameter doesn't commit it, so these are a
// call with type arguments and a type assertion
declare const x: any, async: any; type U = 1;
async <T extends U>(x: any);
<U>(x?: number);
