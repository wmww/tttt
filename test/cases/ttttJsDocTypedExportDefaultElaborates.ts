// `/** @type {T} */ export default e` in JavaScript is related with elaboration into e (tsgo's checkExportAssignment:
// checkTypeAssignableToAndOptionallyElaborate): the error is at the property, not the whole object
// @allowJs: true
// @checkJs: true
// @noEmit: true
// @strict: true
// @filename: t.d.ts
export interface M { meta: { fixable?: "code" | undefined; type: string }; create(c: number): void }
// @filename: a.js
/** @type {import('./t').M} */
export default {
  meta: {
    type: 'layout',
    fixable: null,
  },
  create(c) {},
};
// @filename: b.js
/** @type {import('./t').M} */
const m = {
  meta: {
    type: 'layout',
    fixable: null,
  },
  create(c) {},
};
