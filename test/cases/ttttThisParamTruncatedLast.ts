// @strict: true
// @noErrorTruncation: false
// A signature's parameters count toward a message's truncation length before its `this` parameter (tsc's
// signatureToSignatureDeclarationHelper makes their nodes first): the long `this` type is the one cut short
interface PMNode { type: string; attrs: Record<string, any> }
interface Element { el: true }
declare const el: Element;
type T = { m?: ((this: { aaaaaaaaaaaaaaaaaaaa: string; bbbbbbbbbbbbbbbbbbbbbbbb: number; ccccccccccccccccccccccc: boolean; ddddddddddddddddddddd: Record<string, any>; eeeeeeeeeeeeeeee: Record<string, any>; }, props: { node: PMNode; HTMLAttributes: Record<string, any> }) => string) | null };
const o: T = {
  m({ HTMLAttributes }) { return el },
};
const o2: T = {
  m(props) { return el },
};
