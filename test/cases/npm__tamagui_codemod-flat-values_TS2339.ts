// origin: npm @tamagui/codemod-flat-values@3.1.3 https://github.com/tamagui/tamagui code/core/codemod-flat-values/tsconfig.bench.json seed 20261028
// flip: yes
// A let narrowed by an assignment in a loop, read through an optional chain, then by truthiness: inside a closure past
// its last assignment `node!` is JsxAttribute in tsgo, never in tttt (src/transition.ts:302)
// @strict: true
declare class JsxAttribute { kind: 1; replaceWithText(t: string): void; getInitializer(): string | undefined }
declare class JsxSpreadAttribute { kind: 2 }
declare function isJsxAttribute(n: unknown): n is JsxAttribute;
declare const attrs: (JsxAttribute | JsxSpreadAttribute)[];
export function f() {
  let node: JsxAttribute | JsxSpreadAttribute | null = null;
  for (const attribute of attrs) {
    if (!isJsxAttribute(attribute)) continue;
    node = attribute;
  }
  const initializer = node?.getInitializer();
  if (!node) return;
  return () => node!.replaceWithText("");
}
