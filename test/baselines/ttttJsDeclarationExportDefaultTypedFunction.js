//// [test/cases/ttttJsDeclarationExportDefaultTypedFunction.ts] ////

//// [t.ts]
export type JsPage = (r: Request, c: { pageData: string }) => Promise<string> | string;

//// [a.js]
/** @type {import('./t.js').JsPage} */
export default async (_request, { pageData }) => {
  return pageData;
};

//// [c.js]
/** @type {(a: number) => number} */
export default (a) => a;

//// [f.js]
/** @type {import('./t.js').JsPage} */
export function f(_request, { pageData }) {
  return pageData;
}




//// [t.d.ts]
export type JsPage = (r: Request, c: {
    pageData: string;
}) => Promise<string> | string;
//# sourceMappingURL=t.d.ts.map//// [a.d.ts]
export default _default;
/** @type {import('./t.js').JsPage} */
declare const _default: import('./t.js').JsPage;
//# sourceMappingURL=a.d.ts.map//// [c.d.ts]
export default _default;
/** @type {(a: number) => number} */
declare const _default: (a: number) => number;
//# sourceMappingURL=c.d.ts.map//// [f.d.ts]
/** @type {import('./t.js').JsPage} */
export declare function f(_request: Request, { pageData }: {
    pageData: string;
}): string | Promise<string>;
//# sourceMappingURL=f.d.ts.map