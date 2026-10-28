// @noEmit: true
// A reserved word where a parameter starts is TS1359 and a missing name (tsc's parseNameOfParameter): the missing
// identifier is at the parameter's full start, where TS2300 '(Missing)' goes, after the comma before it (mutation
// testing seed 20261101, conditionalPromptInference); TS7006 is at the parameter, after the trivia
declare function f(a: number,

const b: number, c: number,
  const d: string): void;
