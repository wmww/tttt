// @target: es2015
// @noTypesAndSymbols: true
// An object literal method's private name is tsc's checkGrammarObjectLiteralExpression TS18016 alone, a grammar error
// that a parse error hides (checkMethodDeclaration only sees a class's methods): mutation seed 20261106,
// privateNameInObjectLiteral-2 missing its `}`
const obj = {
    #foo() {
    }
