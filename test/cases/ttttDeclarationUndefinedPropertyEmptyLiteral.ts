// The `?: undefined` property a union of object literals gets next to a sibling's `b: {}` is declared by that
// property (tsc's getUndefinedProperty), and an empty object literal says any type without properties (tsgo's
// pseudoTypeEquivalentToType compares getPropertiesOfType, empty for undefined): declaration emit prints `b?: {}`
// (slate's test/interfaces/Element/isElementList/not-full-element)
// @strict: true,false
// @declaration: true
export const input = [
  { children: [] },
  { type: 'set_node', path: [0], properties: {}, newProperties: {} },
];
export const other = [{ a: 1 }, { b: { c: 1 } }];
