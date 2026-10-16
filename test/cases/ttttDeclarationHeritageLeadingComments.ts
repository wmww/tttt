// @declaration: true
// The comments on the line before a heritage clause are its leading comments, which tsc emits with it, each and a
// space (graphql-js's `class GraphQLEnumType /* <T> */ implements`); without a clause they're dropped
// @filename: a.ts
export interface S {}
export class E /* <T> */ implements S {}
export class F /* c */ {}
export class G<T> /* c */ implements S {}
export function foo /* c */ () {}
export const v /* c */ = 1;
export interface I /* c */ { a: 1 }
export type A /* c */ = 1;
// @filename: b.ts
export class B {}
export class H /* c */ extends B {}
export class J /* c */ extends B /* d */ implements Object /* e */ {}
export class K /* c */
  // line
  extends B {}
// @filename: c.ts
export interface J {}
export interface I /* c */ extends J {}
export interface I2<T> /* c */ extends J /* d */ {}
