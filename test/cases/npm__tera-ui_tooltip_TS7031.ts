// origin: npm @tera-ui/tooltip@1.3.3 https://github.com/hieumau12/tera-ui packages/components/tooltip/tsconfig.bench.json seed 20261025
// flip: no
// A computed non-literal key in an unannotated parameter's binding pattern: the implied type leaves it out, so the element
// indexes it by string, any with a rest element (no TS7031), TS2537 without one. tttt reports TS7031 for both
// @strict: true
export const renameProp = (oldProp: string, newProp: string, {[oldProp]: old, ...others}) => ({
  [newProp]: old,
  ...others,
});
export function f(k: string, {[k]: a, b}) { return [a, b]; }
