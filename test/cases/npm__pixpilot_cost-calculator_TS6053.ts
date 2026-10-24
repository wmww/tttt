// origin: npm @pixpilot/cost-calculator@0.1.0 https://github.com/pixpilot/credit-calculator packages/cost-calculator/tsconfig.json seed 20261211
// flip: no
// A tsconfig `extends` of a package subpath whose `exports` has a `./*` pattern before the exact `./typescript`
// key: tsgo resolves the exact key (exact keys beat patterns whatever their order), tttt reports TS6053 and loses
// the base config's options (on the project, tsgo's TS2883 went missing and a zod TS5115 appeared).
// @filename: tsconfig.json
{ "extends": "@pixpilot/dev-config/typescript", "compilerOptions": { "noEmit": true }, "files": ["a.ts"] }
// @filename: node_modules/@pixpilot/dev-config/package.json
{"name": "@pixpilot/dev-config", "version": "3.22.1", "exports": {".": {"import": {"types": "./dist/index.d.ts", "default": "./dist/index.js"}}, "./*": {"import": {"types": "./dist/*.d.ts", "default": "./dist/*.js"}}, "./typescript": "./dist/tsconfig-base.json"}}
// @filename: node_modules/@pixpilot/dev-config/dist/tsconfig-base.json
{ "compilerOptions": { "strict": true } }
// @filename: a.ts
export const f = (x) => x;
