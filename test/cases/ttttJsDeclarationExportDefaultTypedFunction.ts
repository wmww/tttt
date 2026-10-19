// @allowJs: true
// @checkJs: true
// @declaration: true
// @declarationMap: true
// @emitDeclarationOnly: true
// @strict: true
// @target: es2022
// @module: esnext
// `/** @type {T} */ export default () => {}`: T is the export assignment's type (tsgo's reparser), written as `declare
// const _default: T` after the export; a function declaration typed by a JSDoc `@type` writes its binding pattern
// parameter's contextual type (rocket's pages)

// @filename: t.ts
export type JsPage = (r: Request, c: { pageData: string }) => Promise<string> | string;

// @filename: a.js
/** @type {import('./t.js').JsPage} */
export default async (_request, { pageData }) => {
  return pageData;
};

// @filename: c.js
/** @type {(a: number) => number} */
export default (a) => a;

// @filename: f.js
/** @type {import('./t.js').JsPage} */
export function f(_request, { pageData }) {
  return pageData;
}
