// @noImplicitReferences: true

// A config diagnostic of a config the tsconfig extends is at that config in the baseline (the harness's side of the
// CLI's program_extended_config_error), not without a location
// @filename: /project/base.json
{ "compilerOptions": { "outDir": 42 } }

// @filename: /project/src/index.ts
export const value = 0;

// @filename: /project/tsconfig.json
{ "extends": "./base.json", "compilerOptions": {}, "files": ["src/index.ts"] }
