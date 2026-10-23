// A misplaced 'readonly' type operator is TS1354 from tsc's checker (checkGrammarTypeOperatorNode), a grammar error:
// not reported in a file with parse errors (babel's types/read-only-* fixtures, a project with syntax errors)
type T30 = readonly string;
let x = ;
