// An `infer` outside a conditional's extends clause is tsgo's grammar error (TS1338, grammarErrorOnNode): not
// reported in a file with parse errors (mutation seed 20261107, contextualReturnArrayPatternCallback)
type ArrayElement<Input> = Input extends Object (infer Value)[] ? Value : unknown;
type Outside<T> = (infer U)[];
