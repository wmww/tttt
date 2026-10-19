// A side-effect import of a .mts/.ts path is no TS5097 (tsgo's resolveExternalModule reports it only in an
// IsEmittableImport, which an import declaration is only with a clause): angular-cli's `import './init-env.mts'`
// @module: esnext
// @moduleResolution: bundler
// @target: es2022
// @filename: /init-env.mts
export {};
// @filename: /other.mts
export const x = 1;
// @filename: /a.mts
import './init-env.mts';
import { x } from './other.mts';
export const y = x;
