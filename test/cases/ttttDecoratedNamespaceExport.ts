// @allowJs: true
// @checkJs: true
// @noEmit: true
// @filename: a.ts
declare function dec(...a: any[]): any;
@dec export as namespace X;
// @filename: b.js
@dec export as namespace Y;
