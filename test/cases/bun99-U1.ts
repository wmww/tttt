// @noTypesAndSymbols: true
// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "target": "es2022", "module": "esnext", "moduleResolution": "bundler", "types": []}, "files": ["fn_type__own.ts", "fn_type__none.ts", "fn_type__outer_alias.ts", "ctor_type__own.ts", "ctor_type__none.ts", "ctor_type__outer_alias.ts", "call_sig__own.ts", "call_sig__none.ts", "call_sig__outer_alias.ts", "construct_sig__own.ts", "construct_sig__none.ts", "construct_sig__outer_alias.ts", "method_sig__own.ts", "method_sig__none.ts", "method_sig__outer_alias.ts", "literal_method__own.ts", "literal_method__none.ts", "literal_method__outer_alias.ts", "declare_fn__own.ts", "declare_fn__none.ts", "declare_fn__outer_alias.ts", "overload__own.ts", "overload__none.ts", "overload__outer_alias.ts", "abstract__own.ts", "abstract__none.ts", "abstract__outer_alias.ts", "declare_method__own.ts", "declare_method__none.ts", "declare_method__outer_alias.ts", "sentinel.ts"]}
// @filename: abstract__none.ts
namespace n_annotation {
  export abstract class D { abstract m<T>({ a: X }: { a: X }): void; }
}
namespace n_return {
  export abstract class D { abstract m<T>({ a: X }: { a: any }): X; }
}
namespace n_other_param {
  export abstract class D { abstract m<T>({ a: X }: { a: any }, b: X): void; }
}
namespace n_unused {
  export abstract class D { abstract m<T>({ a: X }: { a: any }): void; }
}
namespace n_typeof_return {
  export abstract class D { abstract m<T>({ a: X }: { a: any }): typeof X; }
}
namespace n_predicate {
  export abstract class D { abstract m<T>({ a: X }: { a: any }): X is string; }
}
namespace n_asserts {
  export abstract class D { abstract m<T>({ a: X }: { a: any }): asserts X; }
}
namespace n_nested {
  export abstract class D { abstract m<T>({ a: { b: X } }: { a: { b: X } }): void; }
}
namespace n_array {
  export abstract class D { abstract m<T>({ a: [X] }: { a: [X] }): void; }
}
namespace n_no_annotation {
  export abstract class D { abstract m<T>({ a: X }): X; }
}
namespace n_two {
  export abstract class D { abstract m<T>({ a: X, b: Y }: { a: X; b: any }): void; }
}
namespace n_generic_arg {
  export abstract class D { abstract m<T>({ a: X }: { a: Array<X> }): void; }
}
export {};
// @filename: abstract__outer_alias.ts
namespace n_annotation {
  type X = 1;
  export abstract class D { abstract m<T>({ a: X }: { a: X }): void; }
}
namespace n_return {
  type X = 1;
  export abstract class D { abstract m<T>({ a: X }: { a: any }): X; }
}
namespace n_other_param {
  type X = 1;
  export abstract class D { abstract m<T>({ a: X }: { a: any }, b: X): void; }
}
namespace n_unused {
  type X = 1;
  export abstract class D { abstract m<T>({ a: X }: { a: any }): void; }
}
namespace n_typeof_return {
  type X = 1;
  export abstract class D { abstract m<T>({ a: X }: { a: any }): typeof X; }
}
namespace n_predicate {
  type X = 1;
  export abstract class D { abstract m<T>({ a: X }: { a: any }): X is string; }
}
namespace n_asserts {
  type X = 1;
  export abstract class D { abstract m<T>({ a: X }: { a: any }): asserts X; }
}
namespace n_nested {
  type X = 1;
  export abstract class D { abstract m<T>({ a: { b: X } }: { a: { b: X } }): void; }
}
namespace n_array {
  type X = 1;
  export abstract class D { abstract m<T>({ a: [X] }: { a: [X] }): void; }
}
namespace n_no_annotation {
  type X = 1;
  export abstract class D { abstract m<T>({ a: X }): X; }
}
namespace n_two {
  type X = 1;
  export abstract class D { abstract m<T>({ a: X, b: Y }: { a: X; b: any }): void; }
}
namespace n_generic_arg {
  type X = 1;
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
// @filename: call_sig__none.ts
namespace n_annotation {
  export interface D { <T>({ a: X }: { a: X }): void }
}
namespace n_return {
  export interface D { <T>({ a: X }: { a: any }): X }
}
namespace n_other_param {
  export interface D { <T>({ a: X }: { a: any }, b: X): void }
}
namespace n_unused {
  export interface D { <T>({ a: X }: { a: any }): void }
}
namespace n_typeof_return {
  export interface D { <T>({ a: X }: { a: any }): typeof X }
}
namespace n_predicate {
  export interface D { <T>({ a: X }: { a: any }): X is string }
}
namespace n_asserts {
  export interface D { <T>({ a: X }: { a: any }): asserts X }
}
namespace n_nested {
  export interface D { <T>({ a: { b: X } }: { a: { b: X } }): void }
}
namespace n_array {
  export interface D { <T>({ a: [X] }: { a: [X] }): void }
}
namespace n_no_annotation {
  export interface D { <T>({ a: X }): X }
}
namespace n_two {
  export interface D { <T>({ a: X, b: Y }: { a: X; b: any }): void }
}
namespace n_generic_arg {
  export interface D { <T>({ a: X }: { a: Array<X> }): void }
}
export {};
// @filename: call_sig__outer_alias.ts
namespace n_annotation {
  type X = 1;
  export interface D { <T>({ a: X }: { a: X }): void }
}
namespace n_return {
  type X = 1;
  export interface D { <T>({ a: X }: { a: any }): X }
}
namespace n_other_param {
  type X = 1;
  export interface D { <T>({ a: X }: { a: any }, b: X): void }
}
namespace n_unused {
  type X = 1;
  export interface D { <T>({ a: X }: { a: any }): void }
}
namespace n_typeof_return {
  type X = 1;
  export interface D { <T>({ a: X }: { a: any }): typeof X }
}
namespace n_predicate {
  type X = 1;
  export interface D { <T>({ a: X }: { a: any }): X is string }
}
namespace n_asserts {
  type X = 1;
  export interface D { <T>({ a: X }: { a: any }): asserts X }
}
namespace n_nested {
  type X = 1;
  export interface D { <T>({ a: { b: X } }: { a: { b: X } }): void }
}
namespace n_array {
  type X = 1;
  export interface D { <T>({ a: [X] }: { a: [X] }): void }
}
namespace n_no_annotation {
  type X = 1;
  export interface D { <T>({ a: X }): X }
}
namespace n_two {
  type X = 1;
  export interface D { <T>({ a: X, b: Y }: { a: X; b: any }): void }
}
namespace n_generic_arg {
  type X = 1;
  export interface D { <T>({ a: X }: { a: Array<X> }): void }
}
export {};
// @filename: call_sig__own.ts
namespace n_annotation {
  export interface D { <X>({ a: X }: { a: X }): void }
}
namespace n_return {
  export interface D { <X>({ a: X }: { a: any }): X }
}
namespace n_other_param {
  export interface D { <X>({ a: X }: { a: any }, b: X): void }
}
namespace n_unused {
  export interface D { <X>({ a: X }: { a: any }): void }
}
namespace n_typeof_return {
  export interface D { <X>({ a: X }: { a: any }): typeof X }
}
namespace n_predicate {
  export interface D { <X>({ a: X }: { a: any }): X is string }
}
namespace n_asserts {
  export interface D { <X>({ a: X }: { a: any }): asserts X }
}
namespace n_nested {
  export interface D { <X>({ a: { b: X } }: { a: { b: X } }): void }
}
namespace n_array {
  export interface D { <X>({ a: [X] }: { a: [X] }): void }
}
namespace n_no_annotation {
  export interface D { <X>({ a: X }): X }
}
namespace n_two {
  export interface D { <X>({ a: X, b: Y }: { a: X; b: any }): void }
}
namespace n_generic_arg {
  export interface D { <X>({ a: X }: { a: Array<X> }): void }
}
export {};
// @filename: construct_sig__none.ts
namespace n_annotation {
  export interface D { new <T>({ a: X }: { a: X }): void }
}
namespace n_return {
  export interface D { new <T>({ a: X }: { a: any }): X }
}
namespace n_other_param {
  export interface D { new <T>({ a: X }: { a: any }, b: X): void }
}
namespace n_unused {
  export interface D { new <T>({ a: X }: { a: any }): void }
}
namespace n_typeof_return {
  export interface D { new <T>({ a: X }: { a: any }): typeof X }
}
namespace n_nested {
  export interface D { new <T>({ a: { b: X } }: { a: { b: X } }): void }
}
namespace n_array {
  export interface D { new <T>({ a: [X] }: { a: [X] }): void }
}
namespace n_no_annotation {
  export interface D { new <T>({ a: X }): X }
}
namespace n_two {
  export interface D { new <T>({ a: X, b: Y }: { a: X; b: any }): void }
}
namespace n_generic_arg {
  export interface D { new <T>({ a: X }: { a: Array<X> }): void }
}
export {};
// @filename: construct_sig__outer_alias.ts
namespace n_annotation {
  type X = 1;
  export interface D { new <T>({ a: X }: { a: X }): void }
}
namespace n_return {
  type X = 1;
  export interface D { new <T>({ a: X }: { a: any }): X }
}
namespace n_other_param {
  type X = 1;
  export interface D { new <T>({ a: X }: { a: any }, b: X): void }
}
namespace n_unused {
  type X = 1;
  export interface D { new <T>({ a: X }: { a: any }): void }
}
namespace n_typeof_return {
  type X = 1;
  export interface D { new <T>({ a: X }: { a: any }): typeof X }
}
namespace n_nested {
  type X = 1;
  export interface D { new <T>({ a: { b: X } }: { a: { b: X } }): void }
}
namespace n_array {
  type X = 1;
  export interface D { new <T>({ a: [X] }: { a: [X] }): void }
}
namespace n_no_annotation {
  type X = 1;
  export interface D { new <T>({ a: X }): X }
}
namespace n_two {
  type X = 1;
  export interface D { new <T>({ a: X, b: Y }: { a: X; b: any }): void }
}
namespace n_generic_arg {
  type X = 1;
  export interface D { new <T>({ a: X }: { a: Array<X> }): void }
}
export {};
// @filename: construct_sig__own.ts
namespace n_annotation {
  export interface D { new <X>({ a: X }: { a: X }): void }
}
namespace n_return {
  export interface D { new <X>({ a: X }: { a: any }): X }
}
namespace n_other_param {
  export interface D { new <X>({ a: X }: { a: any }, b: X): void }
}
namespace n_unused {
  export interface D { new <X>({ a: X }: { a: any }): void }
}
namespace n_typeof_return {
  export interface D { new <X>({ a: X }: { a: any }): typeof X }
}
namespace n_nested {
  export interface D { new <X>({ a: { b: X } }: { a: { b: X } }): void }
}
namespace n_array {
  export interface D { new <X>({ a: [X] }: { a: [X] }): void }
}
namespace n_no_annotation {
  export interface D { new <X>({ a: X }): X }
}
namespace n_two {
  export interface D { new <X>({ a: X, b: Y }: { a: X; b: any }): void }
}
namespace n_generic_arg {
  export interface D { new <X>({ a: X }: { a: Array<X> }): void }
}
export {};
// @filename: ctor_type__none.ts
namespace n_annotation {
  export type D = new <T>({ a: X }: { a: X }) => void;
}
namespace n_return {
  export type D = new <T>({ a: X }: { a: any }) => X;
}
namespace n_other_param {
  export type D = new <T>({ a: X }: { a: any }, b: X) => void;
}
namespace n_unused {
  export type D = new <T>({ a: X }: { a: any }) => void;
}
namespace n_typeof_return {
  export type D = new <T>({ a: X }: { a: any }) => typeof X;
}
namespace n_nested {
  export type D = new <T>({ a: { b: X } }: { a: { b: X } }) => void;
}
namespace n_array {
  export type D = new <T>({ a: [X] }: { a: [X] }) => void;
}
namespace n_no_annotation {
  export type D = new <T>({ a: X }) => X;
}
namespace n_two {
  export type D = new <T>({ a: X, b: Y }: { a: X; b: any }) => void;
}
namespace n_generic_arg {
  export type D = new <T>({ a: X }: { a: Array<X> }) => void;
}
export {};
// @filename: ctor_type__outer_alias.ts
namespace n_annotation {
  type X = 1;
  export type D = new <T>({ a: X }: { a: X }) => void;
}
namespace n_return {
  type X = 1;
  export type D = new <T>({ a: X }: { a: any }) => X;
}
namespace n_other_param {
  type X = 1;
  export type D = new <T>({ a: X }: { a: any }, b: X) => void;
}
namespace n_unused {
  type X = 1;
  export type D = new <T>({ a: X }: { a: any }) => void;
}
namespace n_typeof_return {
  type X = 1;
  export type D = new <T>({ a: X }: { a: any }) => typeof X;
}
namespace n_nested {
  type X = 1;
  export type D = new <T>({ a: { b: X } }: { a: { b: X } }) => void;
}
namespace n_array {
  type X = 1;
  export type D = new <T>({ a: [X] }: { a: [X] }) => void;
}
namespace n_no_annotation {
  type X = 1;
  export type D = new <T>({ a: X }) => X;
}
namespace n_two {
  type X = 1;
  export type D = new <T>({ a: X, b: Y }: { a: X; b: any }) => void;
}
namespace n_generic_arg {
  type X = 1;
  export type D = new <T>({ a: X }: { a: Array<X> }) => void;
}
export {};
// @filename: ctor_type__own.ts
namespace n_annotation {
  export type D = new <X>({ a: X }: { a: X }) => void;
}
namespace n_return {
  export type D = new <X>({ a: X }: { a: any }) => X;
}
namespace n_other_param {
  export type D = new <X>({ a: X }: { a: any }, b: X) => void;
}
namespace n_unused {
  export type D = new <X>({ a: X }: { a: any }) => void;
}
namespace n_typeof_return {
  export type D = new <X>({ a: X }: { a: any }) => typeof X;
}
namespace n_nested {
  export type D = new <X>({ a: { b: X } }: { a: { b: X } }) => void;
}
namespace n_array {
  export type D = new <X>({ a: [X] }: { a: [X] }) => void;
}
namespace n_no_annotation {
  export type D = new <X>({ a: X }) => X;
}
namespace n_two {
  export type D = new <X>({ a: X, b: Y }: { a: X; b: any }) => void;
}
namespace n_generic_arg {
  export type D = new <X>({ a: X }: { a: Array<X> }) => void;
}
export {};
// @filename: declare_fn__none.ts
namespace n_annotation {
  export declare function d<T>({ a: X }: { a: X }): void;
}
namespace n_return {
  export declare function d<T>({ a: X }: { a: any }): X;
}
namespace n_other_param {
  export declare function d<T>({ a: X }: { a: any }, b: X): void;
}
namespace n_unused {
  export declare function d<T>({ a: X }: { a: any }): void;
}
namespace n_typeof_return {
  export declare function d<T>({ a: X }: { a: any }): typeof X;
}
namespace n_predicate {
  export declare function d<T>({ a: X }: { a: any }): X is string;
}
namespace n_asserts {
  export declare function d<T>({ a: X }: { a: any }): asserts X;
}
namespace n_nested {
  export declare function d<T>({ a: { b: X } }: { a: { b: X } }): void;
}
namespace n_array {
  export declare function d<T>({ a: [X] }: { a: [X] }): void;
}
namespace n_no_annotation {
  export declare function d<T>({ a: X }): X;
}
namespace n_two {
  export declare function d<T>({ a: X, b: Y }: { a: X; b: any }): void;
}
namespace n_generic_arg {
  export declare function d<T>({ a: X }: { a: Array<X> }): void;
}
export {};
// @filename: declare_fn__outer_alias.ts
namespace n_annotation {
  type X = 1;
  export declare function d<T>({ a: X }: { a: X }): void;
}
namespace n_return {
  type X = 1;
  export declare function d<T>({ a: X }: { a: any }): X;
}
namespace n_other_param {
  type X = 1;
  export declare function d<T>({ a: X }: { a: any }, b: X): void;
}
namespace n_unused {
  type X = 1;
  export declare function d<T>({ a: X }: { a: any }): void;
}
namespace n_typeof_return {
  type X = 1;
  export declare function d<T>({ a: X }: { a: any }): typeof X;
}
namespace n_predicate {
  type X = 1;
  export declare function d<T>({ a: X }: { a: any }): X is string;
}
namespace n_asserts {
  type X = 1;
  export declare function d<T>({ a: X }: { a: any }): asserts X;
}
namespace n_nested {
  type X = 1;
  export declare function d<T>({ a: { b: X } }: { a: { b: X } }): void;
}
namespace n_array {
  type X = 1;
  export declare function d<T>({ a: [X] }: { a: [X] }): void;
}
namespace n_no_annotation {
  type X = 1;
  export declare function d<T>({ a: X }): X;
}
namespace n_two {
  type X = 1;
  export declare function d<T>({ a: X, b: Y }: { a: X; b: any }): void;
}
namespace n_generic_arg {
  type X = 1;
  export declare function d<T>({ a: X }: { a: Array<X> }): void;
}
export {};
// @filename: declare_fn__own.ts
namespace n_annotation {
  export declare function d<X>({ a: X }: { a: X }): void;
}
namespace n_return {
  export declare function d<X>({ a: X }: { a: any }): X;
}
namespace n_other_param {
  export declare function d<X>({ a: X }: { a: any }, b: X): void;
}
namespace n_unused {
  export declare function d<X>({ a: X }: { a: any }): void;
}
namespace n_typeof_return {
  export declare function d<X>({ a: X }: { a: any }): typeof X;
}
namespace n_predicate {
  export declare function d<X>({ a: X }: { a: any }): X is string;
}
namespace n_asserts {
  export declare function d<X>({ a: X }: { a: any }): asserts X;
}
namespace n_nested {
  export declare function d<X>({ a: { b: X } }: { a: { b: X } }): void;
}
namespace n_array {
  export declare function d<X>({ a: [X] }: { a: [X] }): void;
}
namespace n_no_annotation {
  export declare function d<X>({ a: X }): X;
}
namespace n_two {
  export declare function d<X>({ a: X, b: Y }: { a: X; b: any }): void;
}
namespace n_generic_arg {
  export declare function d<X>({ a: X }: { a: Array<X> }): void;
}
export {};
// @filename: declare_method__none.ts
namespace n_annotation {
  export declare class D { m<T>({ a: X }: { a: X }): void; }
}
namespace n_return {
  export declare class D { m<T>({ a: X }: { a: any }): X; }
}
namespace n_other_param {
  export declare class D { m<T>({ a: X }: { a: any }, b: X): void; }
}
namespace n_unused {
  export declare class D { m<T>({ a: X }: { a: any }): void; }
}
namespace n_typeof_return {
  export declare class D { m<T>({ a: X }: { a: any }): typeof X; }
}
namespace n_predicate {
  export declare class D { m<T>({ a: X }: { a: any }): X is string; }
}
namespace n_asserts {
  export declare class D { m<T>({ a: X }: { a: any }): asserts X; }
}
namespace n_nested {
  export declare class D { m<T>({ a: { b: X } }: { a: { b: X } }): void; }
}
namespace n_array {
  export declare class D { m<T>({ a: [X] }: { a: [X] }): void; }
}
namespace n_no_annotation {
  export declare class D { m<T>({ a: X }): X; }
}
namespace n_two {
  export declare class D { m<T>({ a: X, b: Y }: { a: X; b: any }): void; }
}
namespace n_generic_arg {
  export declare class D { m<T>({ a: X }: { a: Array<X> }): void; }
}
export {};
// @filename: declare_method__outer_alias.ts
namespace n_annotation {
  type X = 1;
  export declare class D { m<T>({ a: X }: { a: X }): void; }
}
namespace n_return {
  type X = 1;
  export declare class D { m<T>({ a: X }: { a: any }): X; }
}
namespace n_other_param {
  type X = 1;
  export declare class D { m<T>({ a: X }: { a: any }, b: X): void; }
}
namespace n_unused {
  type X = 1;
  export declare class D { m<T>({ a: X }: { a: any }): void; }
}
namespace n_typeof_return {
  type X = 1;
  export declare class D { m<T>({ a: X }: { a: any }): typeof X; }
}
namespace n_predicate {
  type X = 1;
  export declare class D { m<T>({ a: X }: { a: any }): X is string; }
}
namespace n_asserts {
  type X = 1;
  export declare class D { m<T>({ a: X }: { a: any }): asserts X; }
}
namespace n_nested {
  type X = 1;
  export declare class D { m<T>({ a: { b: X } }: { a: { b: X } }): void; }
}
namespace n_array {
  type X = 1;
  export declare class D { m<T>({ a: [X] }: { a: [X] }): void; }
}
namespace n_no_annotation {
  type X = 1;
  export declare class D { m<T>({ a: X }): X; }
}
namespace n_two {
  type X = 1;
  export declare class D { m<T>({ a: X, b: Y }: { a: X; b: any }): void; }
}
namespace n_generic_arg {
  type X = 1;
  export declare class D { m<T>({ a: X }: { a: Array<X> }): void; }
}
export {};
// @filename: declare_method__own.ts
namespace n_annotation {
  export declare class D { m<X>({ a: X }: { a: X }): void; }
}
namespace n_return {
  export declare class D { m<X>({ a: X }: { a: any }): X; }
}
namespace n_other_param {
  export declare class D { m<X>({ a: X }: { a: any }, b: X): void; }
}
namespace n_unused {
  export declare class D { m<X>({ a: X }: { a: any }): void; }
}
namespace n_typeof_return {
  export declare class D { m<X>({ a: X }: { a: any }): typeof X; }
}
namespace n_predicate {
  export declare class D { m<X>({ a: X }: { a: any }): X is string; }
}
namespace n_asserts {
  export declare class D { m<X>({ a: X }: { a: any }): asserts X; }
}
namespace n_nested {
  export declare class D { m<X>({ a: { b: X } }: { a: { b: X } }): void; }
}
namespace n_array {
  export declare class D { m<X>({ a: [X] }: { a: [X] }): void; }
}
namespace n_no_annotation {
  export declare class D { m<X>({ a: X }): X; }
}
namespace n_two {
  export declare class D { m<X>({ a: X, b: Y }: { a: X; b: any }): void; }
}
namespace n_generic_arg {
  export declare class D { m<X>({ a: X }: { a: Array<X> }): void; }
}
export {};
// @filename: fn_type__none.ts
namespace n_annotation {
  export type D = <T>({ a: X }: { a: X }) => void;
}
namespace n_return {
  export type D = <T>({ a: X }: { a: any }) => X;
}
namespace n_other_param {
  export type D = <T>({ a: X }: { a: any }, b: X) => void;
}
namespace n_unused {
  export type D = <T>({ a: X }: { a: any }) => void;
}
namespace n_typeof_return {
  export type D = <T>({ a: X }: { a: any }) => typeof X;
}
namespace n_predicate {
  export type D = <T>({ a: X }: { a: any }) => X is string;
}
namespace n_asserts {
  export type D = <T>({ a: X }: { a: any }) => asserts X;
}
namespace n_nested {
  export type D = <T>({ a: { b: X } }: { a: { b: X } }) => void;
}
namespace n_array {
  export type D = <T>({ a: [X] }: { a: [X] }) => void;
}
namespace n_no_annotation {
  export type D = <T>({ a: X }) => X;
}
namespace n_two {
  export type D = <T>({ a: X, b: Y }: { a: X; b: any }) => void;
}
namespace n_generic_arg {
  export type D = <T>({ a: X }: { a: Array<X> }) => void;
}
export {};
// @filename: fn_type__outer_alias.ts
namespace n_annotation {
  type X = 1;
  export type D = <T>({ a: X }: { a: X }) => void;
}
namespace n_return {
  type X = 1;
  export type D = <T>({ a: X }: { a: any }) => X;
}
namespace n_other_param {
  type X = 1;
  export type D = <T>({ a: X }: { a: any }, b: X) => void;
}
namespace n_unused {
  type X = 1;
  export type D = <T>({ a: X }: { a: any }) => void;
}
namespace n_typeof_return {
  type X = 1;
  export type D = <T>({ a: X }: { a: any }) => typeof X;
}
namespace n_predicate {
  type X = 1;
  export type D = <T>({ a: X }: { a: any }) => X is string;
}
namespace n_asserts {
  type X = 1;
  export type D = <T>({ a: X }: { a: any }) => asserts X;
}
namespace n_nested {
  type X = 1;
  export type D = <T>({ a: { b: X } }: { a: { b: X } }) => void;
}
namespace n_array {
  type X = 1;
  export type D = <T>({ a: [X] }: { a: [X] }) => void;
}
namespace n_no_annotation {
  type X = 1;
  export type D = <T>({ a: X }) => X;
}
namespace n_two {
  type X = 1;
  export type D = <T>({ a: X, b: Y }: { a: X; b: any }) => void;
}
namespace n_generic_arg {
  type X = 1;
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
// @filename: literal_method__none.ts
namespace n_annotation {
  export type D = { m<T>({ a: X }: { a: X }): void };
}
namespace n_return {
  export type D = { m<T>({ a: X }: { a: any }): X };
}
namespace n_other_param {
  export type D = { m<T>({ a: X }: { a: any }, b: X): void };
}
namespace n_unused {
  export type D = { m<T>({ a: X }: { a: any }): void };
}
namespace n_typeof_return {
  export type D = { m<T>({ a: X }: { a: any }): typeof X };
}
namespace n_predicate {
  export type D = { m<T>({ a: X }: { a: any }): X is string };
}
namespace n_asserts {
  export type D = { m<T>({ a: X }: { a: any }): asserts X };
}
namespace n_nested {
  export type D = { m<T>({ a: { b: X } }: { a: { b: X } }): void };
}
namespace n_array {
  export type D = { m<T>({ a: [X] }: { a: [X] }): void };
}
namespace n_no_annotation {
  export type D = { m<T>({ a: X }): X };
}
namespace n_two {
  export type D = { m<T>({ a: X, b: Y }: { a: X; b: any }): void };
}
namespace n_generic_arg {
  export type D = { m<T>({ a: X }: { a: Array<X> }): void };
}
export {};
// @filename: literal_method__outer_alias.ts
namespace n_annotation {
  type X = 1;
  export type D = { m<T>({ a: X }: { a: X }): void };
}
namespace n_return {
  type X = 1;
  export type D = { m<T>({ a: X }: { a: any }): X };
}
namespace n_other_param {
  type X = 1;
  export type D = { m<T>({ a: X }: { a: any }, b: X): void };
}
namespace n_unused {
  type X = 1;
  export type D = { m<T>({ a: X }: { a: any }): void };
}
namespace n_typeof_return {
  type X = 1;
  export type D = { m<T>({ a: X }: { a: any }): typeof X };
}
namespace n_predicate {
  type X = 1;
  export type D = { m<T>({ a: X }: { a: any }): X is string };
}
namespace n_asserts {
  type X = 1;
  export type D = { m<T>({ a: X }: { a: any }): asserts X };
}
namespace n_nested {
  type X = 1;
  export type D = { m<T>({ a: { b: X } }: { a: { b: X } }): void };
}
namespace n_array {
  type X = 1;
  export type D = { m<T>({ a: [X] }: { a: [X] }): void };
}
namespace n_no_annotation {
  type X = 1;
  export type D = { m<T>({ a: X }): X };
}
namespace n_two {
  type X = 1;
  export type D = { m<T>({ a: X, b: Y }: { a: X; b: any }): void };
}
namespace n_generic_arg {
  type X = 1;
  export type D = { m<T>({ a: X }: { a: Array<X> }): void };
}
export {};
// @filename: literal_method__own.ts
namespace n_annotation {
  export type D = { m<X>({ a: X }: { a: X }): void };
}
namespace n_return {
  export type D = { m<X>({ a: X }: { a: any }): X };
}
namespace n_other_param {
  export type D = { m<X>({ a: X }: { a: any }, b: X): void };
}
namespace n_unused {
  export type D = { m<X>({ a: X }: { a: any }): void };
}
namespace n_typeof_return {
  export type D = { m<X>({ a: X }: { a: any }): typeof X };
}
namespace n_predicate {
  export type D = { m<X>({ a: X }: { a: any }): X is string };
}
namespace n_asserts {
  export type D = { m<X>({ a: X }: { a: any }): asserts X };
}
namespace n_nested {
  export type D = { m<X>({ a: { b: X } }: { a: { b: X } }): void };
}
namespace n_array {
  export type D = { m<X>({ a: [X] }: { a: [X] }): void };
}
namespace n_no_annotation {
  export type D = { m<X>({ a: X }): X };
}
namespace n_two {
  export type D = { m<X>({ a: X, b: Y }: { a: X; b: any }): void };
}
namespace n_generic_arg {
  export type D = { m<X>({ a: X }: { a: Array<X> }): void };
}
export {};
// @filename: method_sig__none.ts
namespace n_annotation {
  export interface D { m<T>({ a: X }: { a: X }): void }
}
namespace n_return {
  export interface D { m<T>({ a: X }: { a: any }): X }
}
namespace n_other_param {
  export interface D { m<T>({ a: X }: { a: any }, b: X): void }
}
namespace n_unused {
  export interface D { m<T>({ a: X }: { a: any }): void }
}
namespace n_typeof_return {
  export interface D { m<T>({ a: X }: { a: any }): typeof X }
}
namespace n_predicate {
  export interface D { m<T>({ a: X }: { a: any }): X is string }
}
namespace n_asserts {
  export interface D { m<T>({ a: X }: { a: any }): asserts X }
}
namespace n_nested {
  export interface D { m<T>({ a: { b: X } }: { a: { b: X } }): void }
}
namespace n_array {
  export interface D { m<T>({ a: [X] }: { a: [X] }): void }
}
namespace n_no_annotation {
  export interface D { m<T>({ a: X }): X }
}
namespace n_two {
  export interface D { m<T>({ a: X, b: Y }: { a: X; b: any }): void }
}
namespace n_generic_arg {
  export interface D { m<T>({ a: X }: { a: Array<X> }): void }
}
export {};
// @filename: method_sig__outer_alias.ts
namespace n_annotation {
  type X = 1;
  export interface D { m<T>({ a: X }: { a: X }): void }
}
namespace n_return {
  type X = 1;
  export interface D { m<T>({ a: X }: { a: any }): X }
}
namespace n_other_param {
  type X = 1;
  export interface D { m<T>({ a: X }: { a: any }, b: X): void }
}
namespace n_unused {
  type X = 1;
  export interface D { m<T>({ a: X }: { a: any }): void }
}
namespace n_typeof_return {
  type X = 1;
  export interface D { m<T>({ a: X }: { a: any }): typeof X }
}
namespace n_predicate {
  type X = 1;
  export interface D { m<T>({ a: X }: { a: any }): X is string }
}
namespace n_asserts {
  type X = 1;
  export interface D { m<T>({ a: X }: { a: any }): asserts X }
}
namespace n_nested {
  type X = 1;
  export interface D { m<T>({ a: { b: X } }: { a: { b: X } }): void }
}
namespace n_array {
  type X = 1;
  export interface D { m<T>({ a: [X] }: { a: [X] }): void }
}
namespace n_no_annotation {
  type X = 1;
  export interface D { m<T>({ a: X }): X }
}
namespace n_two {
  type X = 1;
  export interface D { m<T>({ a: X, b: Y }: { a: X; b: any }): void }
}
namespace n_generic_arg {
  type X = 1;
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
// @filename: overload__none.ts
namespace n_annotation {
  export function d<T>({ a: X }: { a: X }): void;
  export function d(...args: any[]): any { return args; }
}
namespace n_return {
  export function d<T>({ a: X }: { a: any }): X;
  export function d(...args: any[]): any { return args; }
}
namespace n_other_param {
  export function d<T>({ a: X }: { a: any }, b: X): void;
  export function d(...args: any[]): any { return args; }
}
namespace n_unused {
  export function d<T>({ a: X }: { a: any }): void;
  export function d(...args: any[]): any { return args; }
}
namespace n_typeof_return {
  export function d<T>({ a: X }: { a: any }): typeof X;
  export function d(...args: any[]): any { return args; }
}
namespace n_predicate {
  export function d<T>({ a: X }: { a: any }): X is string;
  export function d(...args: any[]): any { return args; }
}
namespace n_asserts {
  export function d<T>({ a: X }: { a: any }): asserts X;
  export function d(...args: any[]): any { return args; }
}
namespace n_nested {
  export function d<T>({ a: { b: X } }: { a: { b: X } }): void;
  export function d(...args: any[]): any { return args; }
}
namespace n_array {
  export function d<T>({ a: [X] }: { a: [X] }): void;
  export function d(...args: any[]): any { return args; }
}
namespace n_no_annotation {
  export function d<T>({ a: X }): X;
  export function d(...args: any[]): any { return args; }
}
namespace n_two {
  export function d<T>({ a: X, b: Y }: { a: X; b: any }): void;
  export function d(...args: any[]): any { return args; }
}
namespace n_generic_arg {
  export function d<T>({ a: X }: { a: Array<X> }): void;
  export function d(...args: any[]): any { return args; }
}
export {};
// @filename: overload__outer_alias.ts
namespace n_annotation {
  type X = 1;
  export function d<T>({ a: X }: { a: X }): void;
  export function d(...args: any[]): any { return args; }
}
namespace n_return {
  type X = 1;
  export function d<T>({ a: X }: { a: any }): X;
  export function d(...args: any[]): any { return args; }
}
namespace n_other_param {
  type X = 1;
  export function d<T>({ a: X }: { a: any }, b: X): void;
  export function d(...args: any[]): any { return args; }
}
namespace n_unused {
  type X = 1;
  export function d<T>({ a: X }: { a: any }): void;
  export function d(...args: any[]): any { return args; }
}
namespace n_typeof_return {
  type X = 1;
  export function d<T>({ a: X }: { a: any }): typeof X;
  export function d(...args: any[]): any { return args; }
}
namespace n_predicate {
  type X = 1;
  export function d<T>({ a: X }: { a: any }): X is string;
  export function d(...args: any[]): any { return args; }
}
namespace n_asserts {
  type X = 1;
  export function d<T>({ a: X }: { a: any }): asserts X;
  export function d(...args: any[]): any { return args; }
}
namespace n_nested {
  type X = 1;
  export function d<T>({ a: { b: X } }: { a: { b: X } }): void;
  export function d(...args: any[]): any { return args; }
}
namespace n_array {
  type X = 1;
  export function d<T>({ a: [X] }: { a: [X] }): void;
  export function d(...args: any[]): any { return args; }
}
namespace n_no_annotation {
  type X = 1;
  export function d<T>({ a: X }): X;
  export function d(...args: any[]): any { return args; }
}
namespace n_two {
  type X = 1;
  export function d<T>({ a: X, b: Y }: { a: X; b: any }): void;
  export function d(...args: any[]): any { return args; }
}
namespace n_generic_arg {
  type X = 1;
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
// @filename: sentinel.ts
export const sentinel: number = "";
