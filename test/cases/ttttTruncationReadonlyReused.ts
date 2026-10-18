// @noErrorTruncation: false
// @strict: true
// A readonly property printed from its declaration's syntax still counts its modifier towards the truncation length
// (tsc's addPropertyToElementList: an `as const` literal's members, inbox-zero's error.test.ts)
function make() {
  return {
    attempted: true,
    successful: false,
    label: "Categorize sender",
    provider: "openai",
    model: "gpt-test",
    inputLength: 12,
    inputFingerprint: "abc123",
    startsWithQuote: true,
    startsWithBrace: false,
    startsWithBracket: false,
    looksCodeFenced: false,
    candidateKindsTried: ["trimmed", "original"],
  } as const;
}
interface M { candidateKindsTried: string[] }
declare function take(m: M): void;
take(make());
const x = make();
take(x);
