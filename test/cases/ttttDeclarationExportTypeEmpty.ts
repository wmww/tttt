// @declaration: true
// @declarationMap: true
// `export type {};` keeps its `type` in the declaration file (kitajs's test/with-dist/src/index.ts)
// @filename: a.ts
// a file that is a module only
export type {};
// @filename: b.ts
export type   {   } ;
const x = 1;
