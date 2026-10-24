// origin: npm react-native-reanimated (docs/docs-worklets) https://github.com/software-mansion/react-native-reanimated docs/docs-worklets/tsconfig.bench.json seed 20261302
// flip: no
// @allowJs: true
// @checkJs: true
// @noEmit: true
// @module: esnext
// @moduleResolution: bundler
// @filename: src/cfg.ts
export const TOP = { zones: [] as string[] };
// @filename: docusaurus.config.js
// @ts-check
// @ts-expect-error -- .ts extension is intentional; not type-checked by tsc here.
const { TOP } = require('./src/cfg.ts');
const z = TOP.zones[0];
