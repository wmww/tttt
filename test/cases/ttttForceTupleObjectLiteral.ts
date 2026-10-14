// @strict: true
// @noEmit: true
// forceTuple reaches an object literal's property values (checkObjectLiteral passes the check mode on), and the element's
// message names the literal as checked again with that type as context: `options: [{...}, {...}]` (gemini-cli)
interface Q { question: string; options: { label: string }[]; type: 'a' | 'b' }
const qs: Q[] = [
  { question: 'x', options: [{ label: 'a' }, { label: 'b' }] },
];
