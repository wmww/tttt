// @noTypesAndSymbols: true
// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "target": "es2022", "module": "esnext", "moduleResolution": "bundler", "types": [], "noUnusedLocals": true, "noUnusedParameters": true}, "files": ["fn_type__own.ts", "fn_type__own_second.ts", "fn_type__outer_value.ts", "method_sig__own.ts", "method_sig__own_second.ts", "method_sig__outer_value.ts", "overload__own.ts", "overload__own_second.ts", "overload__outer_value.ts", "abstract__own.ts", "abstract__own_second.ts", "abstract__outer_value.ts", "sentinel.ts"]}
// @filename: abstract__outer_value.ts
namespace n_annotation {
  declare const X: 1;
  export abstract class D { abstract m<T>({ a: X }: { a: X }): void; }
}
namespace n_return {
  declare const X: 1;
  export abstract class D { abstract m<T>({ a: X }: { a: any }): X; }
}
namespace n_other_param {
  declare const X: 1;
  export abstract class D { abstract m<T>({ a: X }: { a: any }, b: X): void; }
}
namespace n_unused {
  declare const X: 1;
  export abstract class D { abstract m<T>({ a: X }: { a: any }): void; }
}
namespace n_typeof_return {
  declare const X: 1;
  export abstract class D { abstract m<T>({ a: X }: { a: any }): typeof X; }
}
namespace n_predicate {
  declare const X: 1;
  export abstract class D { abstract m<T>({ a: X }: { a: any }): X is string; }
}
namespace n_asserts {
  declare const X: 1;
  export abstract class D { abstract m<T>({ a: X }: { a: any }): asserts X; }
}
namespace n_nested {
  declare const X: 1;
  export abstract class D { abstract m<T>({ a: { b: X } }: { a: { b: X } }): void; }
}
namespace n_array {
  declare const X: 1;
  export abstract class D { abstract m<T>({ a: [X] }: { a: [X] }): void; }
}
namespace n_no_annotation {
  declare const X: 1;
  export abstract class D { abstract m<T>({ a: X }): X; }
}
namespace n_two {
  declare const X: 1;
  export abstract class D { abstract m<T>({ a: X, b: Y }: { a: X; b: any }): void; }
}
namespace n_generic_arg {
  declare const X: 1;
  export abstract class D { abstract m<T>({ a: X }: { a: Array<X> }): void; }
}
export {};
// @filename: abstract__own.ts
namespace n_annotation {
  export abstract class D { abstract m<X>({ a: X }: { a: X }): void; }
}
namespace n_return {
  export abstract class D { abstract m<X>({ a: X }: { a: any }): X; }
}
namespace n_other_param {
  export abstract class D { abstract m<X>({ a: X }: { a: any }, b: X): void; }
}
namespace n_unused {
  export abstract class D { abstract m<X>({ a: X }: { a: any }): void; }
}
namespace n_typeof_return {
  export abstract class D { abstract m<X>({ a: X }: { a: any }): typeof X; }
}
namespace n_predicate {
  export abstract class D { abstract m<X>({ a: X }: { a: any }): X is string; }
}
namespace n_asserts {
  export abstract class D { abstract m<X>({ a: X }: { a: any }): asserts X; }
}
namespace n_nested {
  export abstract class D { abstract m<X>({ a: { b: X } }: { a: { b: X } }): void; }
}
namespace n_array {
  export abstract class D { abstract m<X>({ a: [X] }: { a: [X] }): void; }
}
namespace n_no_annotation {
  export abstract class D { abstract m<X>({ a: X }): X; }
}
namespace n_two {
  export abstract class D { abstract m<X>({ a: X, b: Y }: { a: X; b: any }): void; }
}
namespace n_generic_arg {
  export abstract class D { abstract m<X>({ a: X }: { a: Array<X> }): void; }
}
export {};
// @filename: abstract__own_second.ts
namespace n_annotation {
  export abstract class D { abstract m<T, X>({ a: X }: { a: X }): void; }
}
namespace n_return {
  export abstract class D { abstract m<T, X>({ a: X }: { a: any }): X; }
}
namespace n_other_param {
  export abstract class D { abstract m<T, X>({ a: X }: { a: any }, b: X): void; }
}
namespace n_unused {
  export abstract class D { abstract m<T, X>({ a: X }: { a: any }): void; }
}
namespace n_typeof_return {
  export abstract class D { abstract m<T, X>({ a: X }: { a: any }): typeof X; }
}
namespace n_predicate {
  export abstract class D { abstract m<T, X>({ a: X }: { a: any }): X is string; }
}
namespace n_asserts {
  export abstract class D { abstract m<T, X>({ a: X }: { a: any }): asserts X; }
}
namespace n_nested {
  export abstract class D { abstract m<T, X>({ a: { b: X } }: { a: { b: X } }): void; }
}
namespace n_array {
  export abstract class D { abstract m<T, X>({ a: [X] }: { a: [X] }): void; }
}
namespace n_no_annotation {
  export abstract class D { abstract m<T, X>({ a: X }): X; }
}
namespace n_two {
  export abstract class D { abstract m<T, X>({ a: X, b: Y }: { a: X; b: any }): void; }
}
namespace n_generic_arg {
  export abstract class D { abstract m<T, X>({ a: X }: { a: Array<X> }): void; }
}
export {};
// @filename: fn_type__outer_value.ts
namespace n_annotation {
  declare const X: 1;
  export type D = <T>({ a: X }: { a: X }) => void;
}
namespace n_return {
  declare const X: 1;
  export type D = <T>({ a: X }: { a: any }) => X;
}
namespace n_other_param {
  declare const X: 1;
  export type D = <T>({ a: X }: { a: any }, b: X) => void;
}
namespace n_unused {
  declare const X: 1;
  export type D = <T>({ a: X }: { a: any }) => void;
}
namespace n_typeof_return {
  declare const X: 1;
  export type D = <T>({ a: X }: { a: any }) => typeof X;
}
namespace n_predicate {
  declare const X: 1;
  export type D = <T>({ a: X }: { a: any }) => X is string;
}
namespace n_asserts {
  declare const X: 1;
  export type D = <T>({ a: X }: { a: any }) => asserts X;
}
namespace n_nested {
  declare const X: 1;
  export type D = <T>({ a: { b: X } }: { a: { b: X } }) => void;
}
namespace n_array {
  declare const X: 1;
  export type D = <T>({ a: [X] }: { a: [X] }) => void;
}
namespace n_no_annotation {
  declare const X: 1;
  export type D = <T>({ a: X }) => X;
}
namespace n_two {
  declare const X: 1;
  export type D = <T>({ a: X, b: Y }: { a: X; b: any }) => void;
}
namespace n_generic_arg {
  declare const X: 1;
  export type D = <T>({ a: X }: { a: Array<X> }) => void;
}
export {};
// @filename: fn_type__own.ts
namespace n_annotation {
  export type D = <X>({ a: X }: { a: X }) => void;
}
namespace n_return {
  export type D = <X>({ a: X }: { a: any }) => X;
}
namespace n_other_param {
  export type D = <X>({ a: X }: { a: any }, b: X) => void;
}
namespace n_unused {
  export type D = <X>({ a: X }: { a: any }) => void;
}
namespace n_typeof_return {
  export type D = <X>({ a: X }: { a: any }) => typeof X;
}
namespace n_predicate {
  export type D = <X>({ a: X }: { a: any }) => X is string;
}
namespace n_asserts {
  export type D = <X>({ a: X }: { a: any }) => asserts X;
}
namespace n_nested {
  export type D = <X>({ a: { b: X } }: { a: { b: X } }) => void;
}
namespace n_array {
  export type D = <X>({ a: [X] }: { a: [X] }) => void;
}
namespace n_no_annotation {
  export type D = <X>({ a: X }) => X;
}
namespace n_two {
  export type D = <X>({ a: X, b: Y }: { a: X; b: any }) => void;
}
namespace n_generic_arg {
  export type D = <X>({ a: X }: { a: Array<X> }) => void;
}
export {};
// @filename: fn_type__own_second.ts
namespace n_annotation {
  export type D = <T, X>({ a: X }: { a: X }) => void;
}
namespace n_return {
  export type D = <T, X>({ a: X }: { a: any }) => X;
}
namespace n_other_param {
  export type D = <T, X>({ a: X }: { a: any }, b: X) => void;
}
namespace n_unused {
  export type D = <T, X>({ a: X }: { a: any }) => void;
}
namespace n_typeof_return {
  export type D = <T, X>({ a: X }: { a: any }) => typeof X;
}
namespace n_predicate {
  export type D = <T, X>({ a: X }: { a: any }) => X is string;
}
namespace n_asserts {
  export type D = <T, X>({ a: X }: { a: any }) => asserts X;
}
namespace n_nested {
  export type D = <T, X>({ a: { b: X } }: { a: { b: X } }) => void;
}
namespace n_array {
  export type D = <T, X>({ a: [X] }: { a: [X] }) => void;
}
namespace n_no_annotation {
  export type D = <T, X>({ a: X }) => X;
}
namespace n_two {
  export type D = <T, X>({ a: X, b: Y }: { a: X; b: any }) => void;
}
namespace n_generic_arg {
  export type D = <T, X>({ a: X }: { a: Array<X> }) => void;
}
export {};
// @filename: method_sig__outer_value.ts
namespace n_annotation {
  declare const X: 1;
  export interface D { m<T>({ a: X }: { a: X }): void }
}
namespace n_return {
  declare const X: 1;
  export interface D { m<T>({ a: X }: { a: any }): X }
}
namespace n_other_param {
  declare const X: 1;
  export interface D { m<T>({ a: X }: { a: any }, b: X): void }
}
namespace n_unused {
  declare const X: 1;
  export interface D { m<T>({ a: X }: { a: any }): void }
}
namespace n_typeof_return {
  declare const X: 1;
  export interface D { m<T>({ a: X }: { a: any }): typeof X }
}
namespace n_predicate {
  declare const X: 1;
  export interface D { m<T>({ a: X }: { a: any }): X is string }
}
namespace n_asserts {
  declare const X: 1;
  export interface D { m<T>({ a: X }: { a: any }): asserts X }
}
namespace n_nested {
  declare const X: 1;
  export interface D { m<T>({ a: { b: X } }: { a: { b: X } }): void }
}
namespace n_array {
  declare const X: 1;
  export interface D { m<T>({ a: [X] }: { a: [X] }): void }
}
namespace n_no_annotation {
  declare const X: 1;
  export interface D { m<T>({ a: X }): X }
}
namespace n_two {
  declare const X: 1;
  export interface D { m<T>({ a: X, b: Y }: { a: X; b: any }): void }
}
namespace n_generic_arg {
  declare const X: 1;
  export interface D { m<T>({ a: X }: { a: Array<X> }): void }
}
export {};
// @filename: method_sig__own.ts
namespace n_annotation {
  export interface D { m<X>({ a: X }: { a: X }): void }
}
namespace n_return {
  export interface D { m<X>({ a: X }: { a: any }): X }
}
namespace n_other_param {
  export interface D { m<X>({ a: X }: { a: any }, b: X): void }
}
namespace n_unused {
  export interface D { m<X>({ a: X }: { a: any }): void }
}
namespace n_typeof_return {
  export interface D { m<X>({ a: X }: { a: any }): typeof X }
}
namespace n_predicate {
  export interface D { m<X>({ a: X }: { a: any }): X is string }
}
namespace n_asserts {
  export interface D { m<X>({ a: X }: { a: any }): asserts X }
}
namespace n_nested {
  export interface D { m<X>({ a: { b: X } }: { a: { b: X } }): void }
}
namespace n_array {
  export interface D { m<X>({ a: [X] }: { a: [X] }): void }
}
namespace n_no_annotation {
  export interface D { m<X>({ a: X }): X }
}
namespace n_two {
  export interface D { m<X>({ a: X, b: Y }: { a: X; b: any }): void }
}
namespace n_generic_arg {
  export interface D { m<X>({ a: X }: { a: Array<X> }): void }
}
export {};
// @filename: method_sig__own_second.ts
namespace n_annotation {
  export interface D { m<T, X>({ a: X }: { a: X }): void }
}
namespace n_return {
  export interface D { m<T, X>({ a: X }: { a: any }): X }
}
namespace n_other_param {
  export interface D { m<T, X>({ a: X }: { a: any }, b: X): void }
}
namespace n_unused {
  export interface D { m<T, X>({ a: X }: { a: any }): void }
}
namespace n_typeof_return {
  export interface D { m<T, X>({ a: X }: { a: any }): typeof X }
}
namespace n_predicate {
  export interface D { m<T, X>({ a: X }: { a: any }): X is string }
}
namespace n_asserts {
  export interface D { m<T, X>({ a: X }: { a: any }): asserts X }
}
namespace n_nested {
  export interface D { m<T, X>({ a: { b: X } }: { a: { b: X } }): void }
}
namespace n_array {
  export interface D { m<T, X>({ a: [X] }: { a: [X] }): void }
}
namespace n_no_annotation {
  export interface D { m<T, X>({ a: X }): X }
}
namespace n_two {
  export interface D { m<T, X>({ a: X, b: Y }: { a: X; b: any }): void }
}
namespace n_generic_arg {
  export interface D { m<T, X>({ a: X }: { a: Array<X> }): void }
}
export {};
// @filename: overload__outer_value.ts
namespace n_annotation {
  declare const X: 1;
  export function d<T>({ a: X }: { a: X }): void;
  export function d(...args: any[]): any { return args; }
}
namespace n_return {
  declare const X: 1;
  export function d<T>({ a: X }: { a: any }): X;
  export function d(...args: any[]): any { return args; }
}
namespace n_other_param {
  declare const X: 1;
  export function d<T>({ a: X }: { a: any }, b: X): void;
  export function d(...args: any[]): any { return args; }
}
namespace n_unused {
  declare const X: 1;
  export function d<T>({ a: X }: { a: any }): void;
  export function d(...args: any[]): any { return args; }
}
namespace n_typeof_return {
  declare const X: 1;
  export function d<T>({ a: X }: { a: any }): typeof X;
  export function d(...args: any[]): any { return args; }
}
namespace n_predicate {
  declare const X: 1;
  export function d<T>({ a: X }: { a: any }): X is string;
  export function d(...args: any[]): any { return args; }
}
namespace n_asserts {
  declare const X: 1;
  export function d<T>({ a: X }: { a: any }): asserts X;
  export function d(...args: any[]): any { return args; }
}
namespace n_nested {
  declare const X: 1;
  export function d<T>({ a: { b: X } }: { a: { b: X } }): void;
  export function d(...args: any[]): any { return args; }
}
namespace n_array {
  declare const X: 1;
  export function d<T>({ a: [X] }: { a: [X] }): void;
  export function d(...args: any[]): any { return args; }
}
namespace n_no_annotation {
  declare const X: 1;
  export function d<T>({ a: X }): X;
  export function d(...args: any[]): any { return args; }
}
namespace n_two {
  declare const X: 1;
  export function d<T>({ a: X, b: Y }: { a: X; b: any }): void;
  export function d(...args: any[]): any { return args; }
}
namespace n_generic_arg {
  declare const X: 1;
  export function d<T>({ a: X }: { a: Array<X> }): void;
  export function d(...args: any[]): any { return args; }
}
export {};
// @filename: overload__own.ts
namespace n_annotation {
  export function d<X>({ a: X }: { a: X }): void;
  export function d(...args: any[]): any { return args; }
}
namespace n_return {
  export function d<X>({ a: X }: { a: any }): X;
  export function d(...args: any[]): any { return args; }
}
namespace n_other_param {
  export function d<X>({ a: X }: { a: any }, b: X): void;
  export function d(...args: any[]): any { return args; }
}
namespace n_unused {
  export function d<X>({ a: X }: { a: any }): void;
  export function d(...args: any[]): any { return args; }
}
namespace n_typeof_return {
  export function d<X>({ a: X }: { a: any }): typeof X;
  export function d(...args: any[]): any { return args; }
}
namespace n_predicate {
  export function d<X>({ a: X }: { a: any }): X is string;
  export function d(...args: any[]): any { return args; }
}
namespace n_asserts {
  export function d<X>({ a: X }: { a: any }): asserts X;
  export function d(...args: any[]): any { return args; }
}
namespace n_nested {
  export function d<X>({ a: { b: X } }: { a: { b: X } }): void;
  export function d(...args: any[]): any { return args; }
}
namespace n_array {
  export function d<X>({ a: [X] }: { a: [X] }): void;
  export function d(...args: any[]): any { return args; }
}
namespace n_no_annotation {
  export function d<X>({ a: X }): X;
  export function d(...args: any[]): any { return args; }
}
namespace n_two {
  export function d<X>({ a: X, b: Y }: { a: X; b: any }): void;
  export function d(...args: any[]): any { return args; }
}
namespace n_generic_arg {
  export function d<X>({ a: X }: { a: Array<X> }): void;
  export function d(...args: any[]): any { return args; }
}
export {};
// @filename: overload__own_second.ts
namespace n_annotation {
  export function d<T, X>({ a: X }: { a: X }): void;
  export function d(...args: any[]): any { return args; }
}
namespace n_return {
  export function d<T, X>({ a: X }: { a: any }): X;
  export function d(...args: any[]): any { return args; }
}
namespace n_other_param {
  export function d<T, X>({ a: X }: { a: any }, b: X): void;
  export function d(...args: any[]): any { return args; }
}
namespace n_unused {
  export function d<T, X>({ a: X }: { a: any }): void;
  export function d(...args: any[]): any { return args; }
}
namespace n_typeof_return {
  export function d<T, X>({ a: X }: { a: any }): typeof X;
  export function d(...args: any[]): any { return args; }
}
namespace n_predicate {
  export function d<T, X>({ a: X }: { a: any }): X is string;
  export function d(...args: any[]): any { return args; }
}
namespace n_asserts {
  export function d<T, X>({ a: X }: { a: any }): asserts X;
  export function d(...args: any[]): any { return args; }
}
namespace n_nested {
  export function d<T, X>({ a: { b: X } }: { a: { b: X } }): void;
  export function d(...args: any[]): any { return args; }
}
namespace n_array {
  export function d<T, X>({ a: [X] }: { a: [X] }): void;
  export function d(...args: any[]): any { return args; }
}
namespace n_no_annotation {
  export function d<T, X>({ a: X }): X;
  export function d(...args: any[]): any { return args; }
}
namespace n_two {
  export function d<T, X>({ a: X, b: Y }: { a: X; b: any }): void;
  export function d(...args: any[]): any { return args; }
}
namespace n_generic_arg {
  export function d<T, X>({ a: X }: { a: Array<X> }): void;
  export function d(...args: any[]): any { return args; }
}
export {};
// @filename: sentinel.ts
export const sentinel: number = "";
