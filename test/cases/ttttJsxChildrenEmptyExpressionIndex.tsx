// @strict: true
// @jsx: preserve
// Elaborating several JSX children against a list or a tuple, tsc's generateJsxChildren gives an empty `{/* */}`
// child an index while the source tuple (checkJsxChildren) has no element for it: the children from there on are
// compared with the next child's type, the empty expression is the error node, the last child is not compared.
// openscience's message-part.tsx and its other `<Show>` lists with comments between the children (TS2322 at the
// comment where tttt reported the last child)
declare global {
  namespace JSX {
    interface Element { e: 1 }
    interface ElementChildrenAttribute { children: {} }
    interface IntrinsicElements { div: { children?: (string | JSX.Element)[] } }
  }
}
declare function Box(props: { children: [JSX.Element, JSX.Element, string] }): JSX.Element;
declare const bad: number;
export const a = <div>
  <div />
  {/* a comment */}
  {bad}
  <div />
  {bad}
</div>;
export const b = <Box>
  <div />
  {/* a comment */}
  {bad}
  text
</Box>;
export const c = <div>
  {bad}
  {/* a comment */}
  <div />
</div>;
