// @declaration: true
// @stripInternal: true
// A type literal member whose computed name isn't late bound is elided before anything in it is checked: the
// local it names isn't kept for it
/**
 * @internal
 */
const lazyMarker = 'lazyMarker' as 'lazyMarker' & {
  __brand: 'lazyMarker';
};
export type Lazy<TAny> = (() => Promise<TAny>) & { [lazyMarker]: true };
const sym: unique symbol = Symbol();
export type K = { [sym]: 1 };
