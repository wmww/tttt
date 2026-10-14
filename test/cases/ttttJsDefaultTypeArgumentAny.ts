// @allowJs: true
// @checkJs: true
// @noEmit: true
// @strict: true
// In JavaScript a class or interface reference without type arguments gets any for a default of unknown or {}
// (tsc's fillMissingTypeArguments with isInJSFile): svelte's @param {Derived} derived, derived.v is any
// @filename: types.d.ts
export interface Value<V = unknown> { v: V }
export interface Derived<V = unknown> extends Value<V> { d: 1 }
// @filename: a.js
/** @import { Derived } from './types' */
/**
 * @template T
 * @param {Derived} derived
 * @returns {T}
 */
export function execute_derived(derived) {
	const q = derived;
	return derived.v;
}
