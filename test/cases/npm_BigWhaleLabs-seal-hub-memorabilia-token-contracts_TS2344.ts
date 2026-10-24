// origin: npm @big-whale-labs/seal-hub-memorabilia-token-contracts@0.0.4 https://github.com/BigWhaleLabs/seal-hub-memorabilia-token-contracts tsconfig.bench.json seed 20261037
// flip: no
// @module: commonjs
// @noImplicitAny: false
// (paths is an object: not expressible as a header) {"*": ["./*"]}
// @resolveJsonModule: true
// @strict: true
// @target: es2020
// @esModuleInterop: true
// @useDefineForClassFields: false
// (over 400 lines: 12 unreduced program files left out: hardhat.config.ts, package.json, scripts/deploy.ts, scripts/generate-inputs.ts, test/nullifierCreatorCircuit.ts, test/nullifierCreatorVerifier.ts ...)
// @filename: node_modules/@ethereum-waffle/provider/package.json
{"name": "@ethereum-waffle/provider", "version": "4.0.5", "main": "dist/cjs/src/index.js", "module": "dist/esm/src/index.ts", "types": "dist/esm/src/index.d.ts"}
// @filename: node_modules/@nomiclabs/hardhat-waffle/package.json
{"name": "@nomiclabs/hardhat-waffle", "version": "2.0.6", "main": "dist/src/index.js", "types": "dist/src/index.d.ts"}
// @filename: node_modules/ethereum-waffle/package.json
{"name": "ethereum-waffle", "version": "4.0.10", "main": "dist/cjs/src/index.js", "module": "dist/esm/src/index.ts", "types": "dist/esm/src/index.d.ts"}
// @filename: node_modules/ganache/package.json
{"name": "ganache", "version": "7.4.3", "main": "dist/node/core.js", "typings": "dist/ganache.d.ts"}
// @filename: package.json
{"name": "@big-whale-labs/seal-hub-memorabilia-token-contracts", "version": "0.0.4", "main": "dist/typechain/index.js", "types": "dist/typechain/index.d.ts"}
// @filename: node_modules/@ethereum-waffle/provider/dist/esm/src/index.d.ts
export * from './revertString';
// @filename: node_modules/@ethereum-waffle/provider/dist/esm/src/revertString.d.ts
import { Provider } from 'ganache';
/**
 */
// @filename: node_modules/@nomiclabs/hardhat-waffle/dist/src/index.d.ts
import "./type-extensions";
// @filename: node_modules/@nomiclabs/hardhat-waffle/dist/src/type-extensions.d.ts
import type { createFixtureLoader, link, loadFixture, MockContract, MockProvider, solidity } from "ethereum-waffle";
// @filename: node_modules/ethereum-waffle/dist/esm/src/index.d.ts
export * from '@ethereum-waffle/provider';
// @filename: node_modules/ganache/dist/ganache.d.ts
declare namespace Base {
    type Config = {
        exclusiveGroups?: ExclusiveGroup[];
    };
}
/**
 */
declare type ExclusiveGroup<C extends Base.Config, K extends ExclusiveGroupIndex<C> = ExclusiveGroupIndex<C>> = ExclusiveGroups<C>[K];
declare type ExclusiveGroupIndex<C extends Base.Config> = number & keyof ExclusiveGroups<C>;
declare type ExclusiveGroupOptionPairs<C extends Base.Config, G extends unknown[]> = G extends [] ? [] : G extends [infer N, ...infer R] ? [
] : never;
declare type ExclusiveGroupOptionsByGroup<C extends Base.Config, G extends ExclusiveGroup<C>> = PairsToMapping<ExclusiveGroupOptionPairs<C, G>>;
declare type ExclusiveGroups<C extends Base.Config> = C["exclusiveGroups"];
/**
 */
