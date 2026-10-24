// origin: npm @ciceksepeti/cui-listbox (0142-ciceksepetitech-cactus-ui) https://github.com/ciceksepetitech/cactus-ui packages/listbox/tsconfig.bench.json seed 20261100
// flip: no
// @strictNullChecks: false
// @noImplicitAny: true
interface Ref<T> { current: T }
interface Render<T, P = {}> { (props: P, ref: Ref<T>): void }
declare function forwardRef<T, P = {}>(render: Render<T, P>): void;
declare const Poly: any;

forwardRef(
  <C extends string>(props: Poly<C, {}>, forwardedRef) => {
  }
);
