// `export default async x => ...` is an async arrow, not an `async` modifier (tsc's nextTokenCanFollowDefaultKeyword:
// after `default`, `async` is a modifier only before `function`)
// @allowJs: true
// @checkJs: true
// @noEmit: true
// @filename: a.js
/** @type {(r: string) => Promise<number>} */
export default async request => {
  return request.length;
};
// @filename: b.ts
export default async request => 1;
// @filename: c.ts
export default async
function f() {}
