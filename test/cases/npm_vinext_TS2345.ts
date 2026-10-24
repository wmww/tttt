// origin: npm vinext@1.3.0 https://github.com/cloudflare/vinext packages/vinext/tsconfig.bench.json seed 20261301
// flip: no
// @strict: true
// @target: es2022
// @noEmit: true
type Comp = (props: {}) => string;
type Mod = Record<string, unknown> & {
  default?: Comp | null | undefined;
};
export function f<TModule extends Mod = Mod>(mods: readonly (TModule | null | undefined)[]) {
  const m = new Map<number, NonNullable<TModule["default"]>>();
  for (const mod of mods) {
    if (mod?.default) {
      m.set(1, mod.default);
    }
  }
}
