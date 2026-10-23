// @strict: true
// @noEmit: true
// An element of a pattern whose type is any is any: its default `{}` isn't read, so it isn't padded with the
// nested pattern's names, which then aren't implicitly any (tsc's getBindingElementTypeFromParentType returns the
// any parent type first). pothos's plugin-with-input fieldWithInput, whose options type was an unresolved any
declare let fn: (options: any) => void;
fn = function ({ typeOptions: { name: typeName, ...rest } = {}, argOptions: { name: argName = 'input', ...a2 } = {}, input }) {
  typeName;
};
const h2: (options: any) => void = ({ t: { n, ...r } = {} }) => n;
// (Not an any parent: padded, TS7031)
const h: (options: { t: any }) => void = ({ t: { n } = {} }) => n;
const g: (options: any) => void = ({ n } = {}) => n;
