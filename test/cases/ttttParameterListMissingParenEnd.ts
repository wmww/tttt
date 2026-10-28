// @strict: true
// @noEmit: true
// A parameter list missing its `)` ends with its last token (tsc's finishNode at the next token's full start), so a
// call signature ending there spans no trivia: TS7020's underline stops at the comma (mutation testing seed 20261102,
// contextualParameterAndSelfReferentialConstraint1)
declare const repeat: {
  <O>(
    options: O,
};
interface I {
  m(a: string,

}
type F = new (a: number,
;
