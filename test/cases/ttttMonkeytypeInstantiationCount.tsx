// Known-failing (monkeytype/frontend SearchableAutoSetting.tsx, reduced with reduce.py): tsgo hits its 5M
// instantiationCount limit relating the JSX attributes of `<form.Field name={props.key} />` against TanStack form's
// DeepKeys and reports TS2589 there (the source's @ts-expect-error covers it); tttt instantiates less and reports the
// TS2322 that follows instead. The depth/instantiation-limit family (bugs/tsgo_divergences.md, sourcesweep_checker.md)
// @allowUmdGlobalAccess: true
// @allowUnreachableCode: false
// @allowUnusedLabels: false
// @exactOptionalPropertyTypes: false
// @forceConsistentCasingInFileNames: true
// @isolatedModules: true
// @jsx: preserve
// @jsxImportSource: solid-js
// @lib: esnext, dom, dom.iterable
// @module: esnext
// @moduleResolution: bundler
// @moduleDetection: force
// @newLine: lf
// @noFallthroughCasesInSwitch: true
// @noImplicitAny: true
// @noImplicitThis: true
// @noImplicitReturns: true
// @noPropertyAccessFromIndexSignature: true
// @noUncheckedIndexedAccess: true
// @noUnusedLocals: false
// @noUnusedParameters: false
// @noImplicitOverride: true
// (paths is an object: not expressible as a header) {"virtual:language-hashes": ["./src/ts/types/virtual-language-hashes.d.ts"], "virtual:env-config": ["./src/ts/types/virtual-env-config.d.ts"]}
// @resolveJsonModule: true
// @skipLibCheck: true
// @strict: true
// @strictBindCallApply: true
// @strictFunctionTypes: true
// @strictNullChecks: true
// @strictPropertyInitialization: true
// @target: es6
// @types: vite/client, node
// @useUnknownInCatchVariables: true
// @alwaysStrict: true
// @esModuleInterop: true
// @preserveConstEnums: true
// @useDefineForClassFields: false
// @filename: frontend/node_modules/@monkeytype/schemas/package.json
{"name": "@monkeytype/schemas", "exports": {".": {"types": "./src/index.ts", "import": "./dist/index.mjs", "require": "./dist/index.js"}, "./*": {"types": "./src/*.ts", "import": "./dist/*.mjs", "require": "./dist/*.js"}}}
// @filename: frontend/node_modules/@tanstack/solid-form/package.json
{"name": "@tanstack/solid-form", "version": "1.33.0", "type": "module", "types": "dist/esm/index.d.ts", "main": "dist/cjs/index.cjs", "module": "dist/esm/index.js", "exports": {".": {"solid": {"types": "./dist/source/index.d.ts", "default": "./dist/source/index.jsx"}, "import": {"types": "./dist/esm/index.d.ts", "default": "./dist/esm/index.js"}, "require": {"types": "./dist/cjs/index.d.cts", "default": "./dist/cjs/index.cjs"}}, "./package.json": "./package.json"}}
// @filename: frontend/node_modules/@types/node/package.json
{"name": "@types/node", "version": "24.9.1", "main": "", "types": "index.d.ts", "typesVersions": {"<=5.6": {"*": ["ts5.6/*"]}, "<=5.7": {"*": ["ts5.7/*"]}}}
// @filename: frontend/node_modules/solid-js/package.json
{"name": "solid-js", "version": "1.9.13", "main": "./dist/server.cjs", "module": "./dist/server.js", "types": "types/index.d.ts", "type": "module", "exports": {".": {"worker": {"types": "./types/index.d.ts", "import": "./dist/server.js", "require": "./dist/server.cjs"}, "browser": {"development": {"types": "./types/index.d.ts", "import": "./dist/dev.js", "require": "./dist/dev.cjs"}, "types": "./types/index.d.ts", "import": "./dist/solid.js", "require": "./dist/solid.cjs"}, "deno": {"types": "./types/index.d.ts", "import": "./dist/server.js", "require": "./dist/server.cjs"}, "node": {"types": "./types/index.d.ts", "import": "./dist/server.js", "require": "./dist/server.cjs"}, "development": {"types": "./types/index.d.ts", "import": "./dist/dev.js", "require": "./dist/dev.cjs"}, "types": "./types/index.d.ts", "import": "./dist/solid.js", "require": "./dist/solid.cjs"}, "./dist/*": "./dist/*", "./types/*": "./types/*", "./jsx-runtime": {"types": "./types/jsx.d.ts", "default": "./dist/solid.js"}, "./jsx-dev-runtime": {"types": "./types/jsx.d.ts", "default": "./dist/solid.js"}, "./store": {"worker": {"types": "./store/types/index.d.ts", "import": "./store/dist/server.js", "require": "./store/dist/server.cjs"}, "browser": {"development": {"types": "./store/types/index.d.ts", "import": "./store/dist/dev.js", "require": "./store/dist/dev.cjs"}, "types": "./store/types/index.d.ts", "import": "./store/dist/store.js", "require": "./store/dist/store.cjs"}, "deno": {"types": "./store/types/index.d.ts", "import": "./store/dist/server.js", "require": "./store/dist/server.cjs"}, "node": {"types": "./store/types/index.d.ts", "import": "./store/dist/server.js", "require": "./store/dist/server.cjs"}, "development": {"types": "./store/types/index.d.ts", "import": "./store/dist/dev.js", "require": "./store/dist/dev.cjs"}, "types": "./store/types/index.d.ts", "import": "./store/dist/store.js", "require": "./store/dist/store.cjs"}, "./store/dist/*": "./store/dist/*", "./store/types/*": "./store/types/*", "./web": {"worker": {"types": "./web/types/index.d.ts", "import": "./web/dist/server.js", "require": "./web/dist/server.cjs"}, "browser": {"development": {"types": "./web/types/index.d.ts", "import": "./web/dist/dev.js", "require": "./web/dist/dev.cjs"}, "types": "./web/types/index.d.ts", "import": "./web/dist/web.js", "require": "./web/dist/web.cjs"}, "deno": {"types": "./web/types/index.d.ts", "import": "./web/dist/server.js", "require": "./web/dist/server.cjs"}, "node": {"types": "./web/types/index.d.ts", "import": "./web/dist/server.js", "require": "./web/dist/server.cjs"}, "development": {"types": "./web/types/index.d.ts", "import": "./web/dist/dev.js", "require": "./web/dist/dev.cjs"}, "types": "./web/types/index.d.ts", "import": "./web/dist/web.js", "require": "./web/dist/web.cjs"}, "./web/storage": {"types": "./web/storage/types/index.d.ts", "import": "./web/storage/dist/storage.js", "require": "./web/storage/dist/storage.cjs"}, "./web/dist/*": "./web/dist/*", "./web/types/*": "./web/types/*", "./universal": {"development": {"types": "./universal/types/index.d.ts", "import": "./universal/dist/dev.js", "require": "./universal/dist/dev.cjs"}, "types": "./universal/types/index.d.ts", "import": "./universal/dist/universal.js", "require": "./universal/dist/universal.cjs"}, "./universal/dist/*": "./universal/dist/*", "./universal/types/*": "./universal/types/*", "./h": {"types": "./h/types/index.d.ts", "import": "./h/dist/h.js", "require": "./h/dist/h.cjs"}, "./h/jsx-runtime": {"types": "./h/jsx-runtime/types/index.d.ts", "import": "./h/jsx-runtime/dist/jsx.js", "require": "./h/jsx-runtime/dist/jsx.cjs"}, "./h/jsx-dev-runtime": {"types": "./h/jsx-runtime/types/index.d.ts", "import": "./h/jsx-runtime/dist/jsx.js", "require": "./h/jsx-runtime/dist/jsx.cjs"}, "./h/dist/*": "./h/dist/*", "./h/types/*": "./h/types/*", "./html": {"types": "./html/types/index.d.ts", "import": "./html/dist/html.js", "require": "./html/dist/html.cjs"}, "./html/dist/*": "./html/dist/*", "./package.json": "./package.json"}}
// @filename: frontend/node_modules/solid-js/store/package.json
{"name": "solid-js/store", "main": "./dist/server.cjs", "module": "./dist/server.js", "types": "./types/index.d.ts", "type": "module", "exports": {".": {"worker": {"types": "./types/index.d.ts", "import": "./dist/server.js", "require": "./dist/server.cjs"}, "browser": {"development": {"types": "./types/index.d.ts", "import": "./dist/dev.js", "require": "./dist/dev.cjs"}, "types": "./types/index.d.ts", "import": "./dist/store.js", "require": "./dist/store.cjs"}, "deno": {"types": "./types/index.d.ts", "import": "./dist/server.js", "require": "./dist/server.cjs"}, "node": {"types": "./types/index.d.ts", "import": "./dist/server.js", "require": "./dist/server.cjs"}, "development": {"types": "./types/index.d.ts", "import": "./dist/dev.js", "require": "./dist/dev.cjs"}, "types": "./types/index.d.ts", "import": "./dist/store.js", "require": "./dist/store.cjs"}}}
// @filename: frontend/node_modules/vite/package.json
{"name": "vite", "version": "8.0.5", "type": "module", "exports": {".": "./dist/node/index.js", "./client": {"types": "./client.d.ts"}, "./module-runner": "./dist/node/module-runner.js", "./internal": "./dist/node/internal.js", "./dist/client/*": "./dist/client/*", "./types/*": {"types": "./types/*"}, "./types/internal/*": null, "./package.json": "./package.json"}, "imports": {"#module-sync-enabled": {"module-sync": "./misc/true.js", "default": "./misc/false.js"}, "#types/*": "./types/*.d.ts", "#dep-types/*": "./src/types/*.d.ts"}}
// @filename: frontend/node_modules/zod/package.json
{"name": "zod", "version": "3.23.8", "main": "./lib/index.js", "module": "./lib/index.mjs", "exports": {".": {"types": "./index.d.ts", "require": "./lib/index.js", "import": "./lib/index.mjs"}, "./package.json": "./package.json", "./locales/*": "./lib/locales/*"}, "types": "./index.d.ts"}
// @filename: frontend/package.json
{"name": "@monkeytype/frontend", "type": "module"}
// @filename: node_modules/@tanstack/form-core/package.json
{"name": "@tanstack/form-core", "version": "1.33.0", "type": "module", "types": "dist/esm/index.d.ts", "main": "dist/cjs/index.cjs", "module": "dist/esm/index.js", "exports": {".": {"import": {"types": "./dist/esm/index.d.ts", "default": "./dist/esm/index.js"}, "require": {"types": "./dist/cjs/index.d.cts", "default": "./dist/cjs/index.cjs"}}, "./package.json": "./package.json"}}
// @filename: package.json
{"name": "monkeytype", "version": "26.32.0", "type": "module"}
// @filename: frontend/node_modules/@monkeytype/schemas/src/configs.ts
import { z, ZodSchema } from "zod";
export const ConfigSchema = z
  .object({
    ads: AdsSchema,
  } satisfies Record<string, ZodSchema>)
export type Config = z.infer<typeof ConfigSchema>;
// @filename: frontend/node_modules/@tanstack/solid-form/dist/esm/createField.d.ts
import { FieldApi, DeepKeys, DeepValue, FieldAsyncValidateOrFn, FieldValidateOrFn, FieldValidators, FormAsyncValidateOrFn, FormValidateOrFn } from '@tanstack/form-core';
/**
 */
export type FieldComponent<in out TParentData, in out TFormOnMount extends undefined | FormValidateOrFn<TParentData>, in out TFormOnChange extends undefined | FormValidateOrFn<TParentData>, in out TFormOnChangeAsync extends undefined | FormAsyncValidateOrFn<TParentData>, in out TFormOnBlur extends undefined | FormValidateOrFn<TParentData>, in out TFormOnBlurAsync extends undefined | FormAsyncValidateOrFn<TParentData>, in out TFormOnSubmit extends undefined | FormValidateOrFn<TParentData>, in out TFormOnSubmitAsync extends undefined | FormAsyncValidateOrFn<TParentData>, in out TFormOnDynamic extends undefined | FormValidateOrFn<TParentData>, in out TFormOnDynamicAsync extends undefined | FormAsyncValidateOrFn<TParentData>, in out TFormOnServer extends undefined | FormAsyncValidateOrFn<TParentData>, in out TPatentSubmitMeta, in out ExtendedApi = {}> = <const TName extends DeepKeys<TParentData>, TData extends DeepValue<TParentData, TName>, TOnMount extends undefined | FieldValidateOrFn<TParentData, TName, TData>, TOnChange extends undefined | FieldValidateOrFn<TParentData, TName, TData>, TOnChangeAsync extends undefined | FieldAsyncValidateOrFn<TParentData, TName, TData>, TOnBlur extends undefined | FieldValidateOrFn<TParentData, TName, TData>, TOnBlurAsync extends undefined | FieldAsyncValidateOrFn<TParentData, TName, TData>, TOnSubmit extends undefined | FieldValidateOrFn<TParentData, TName, TData>, TOnSubmitAsync extends undefined | FieldAsyncValidateOrFn<TParentData, TName, TData>, TOnDynamic extends undefined | FieldValidateOrFn<TParentData, TName, TData>, TOnDynamicAsync extends undefined | FieldAsyncValidateOrFn<TParentData, TName, TData>>({ children, ...fieldOptions }: FieldComponentBoundProps<TParentData, TName, TData, TOnMount, TOnChange, TOnChangeAsync, TOnBlur, TOnBlurAsync, TOnSubmit, TOnSubmitAsync, TOnDynamic, TOnDynamicAsync, TFormOnMount, TFormOnChange, TFormOnChangeAsync, TFormOnBlur, TFormOnBlurAsync, TFormOnSubmit, TFormOnSubmitAsync, TFormOnDynamic, TFormOnDynamicAsync, TFormOnServer, TPatentSubmitMeta, ExtendedApi>) => JSX.Element;
/**
 */
// @filename: frontend/node_modules/@tanstack/solid-form/dist/esm/createForm.d.ts
import { FormApi, FormAsyncValidateOrFn, FormOptions, FormState, FormValidateOrFn } from '@tanstack/form-core';
import { FieldComponent } from './createField.js';
export interface SolidFormApi<TParentData, TFormOnMount extends undefined | FormValidateOrFn<TParentData>, TFormOnChange extends undefined | FormValidateOrFn<TParentData>, TFormOnChangeAsync extends undefined | FormAsyncValidateOrFn<TParentData>, TFormOnBlur extends undefined | FormValidateOrFn<TParentData>, TFormOnBlurAsync extends undefined | FormAsyncValidateOrFn<TParentData>, TFormOnSubmit extends undefined | FormValidateOrFn<TParentData>, TFormOnSubmitAsync extends undefined | FormAsyncValidateOrFn<TParentData>, TFormOnDynamic extends undefined | FormValidateOrFn<TParentData>, TFormOnDynamicAsync extends undefined | FormAsyncValidateOrFn<TParentData>, TFormOnServer extends undefined | FormAsyncValidateOrFn<TParentData>, TSubmitMeta> {
    Field: FieldComponent<TParentData, TFormOnMount, TFormOnChange, TFormOnChangeAsync, TFormOnBlur, TFormOnBlurAsync, TFormOnSubmit, TFormOnSubmitAsync, TFormOnDynamic, TFormOnDynamicAsync, TFormOnServer, TSubmitMeta>;
}
export declare function createForm<TParentData, TFormOnMount extends undefined | FormValidateOrFn<TParentData>, TFormOnChange extends undefined | FormValidateOrFn<TParentData>, TFormOnChangeAsync extends undefined | FormAsyncValidateOrFn<TParentData>, TFormOnBlur extends undefined | FormValidateOrFn<TParentData>, TFormOnBlurAsync extends undefined | FormAsyncValidateOrFn<TParentData>, TFormOnSubmit extends undefined | FormValidateOrFn<TParentData>, TFormOnSubmitAsync extends undefined | FormAsyncValidateOrFn<TParentData>, TFormOnDynamic extends undefined | FormValidateOrFn<TParentData>, TFormOnDynamicAsync extends undefined | FormAsyncValidateOrFn<TParentData>, TFormOnServer extends undefined | FormAsyncValidateOrFn<TParentData>, TSubmitMeta>(opts?: () => FormOptions<TParentData, TFormOnMount, TFormOnChange, TFormOnChangeAsync, TFormOnBlur, TFormOnBlurAsync, TFormOnSubmit, TFormOnSubmitAsync, TFormOnDynamic, TFormOnDynamicAsync, TFormOnServer, TSubmitMeta>): FormApi<TParentData, TFormOnMount, TFormOnChange, TFormOnChangeAsync, TFormOnBlur, TFormOnBlurAsync, TFormOnSubmit, TFormOnSubmitAsync, TFormOnDynamic, TFormOnDynamicAsync, TFormOnServer, TSubmitMeta> & SolidFormApi<TParentData, TFormOnMount, TFormOnChange, TFormOnChangeAsync, TFormOnBlur, TFormOnBlurAsync, TFormOnSubmit, TFormOnSubmitAsync, TFormOnDynamic, TFormOnDynamicAsync, TFormOnServer, TSubmitMeta>;
// @filename: frontend/node_modules/@tanstack/solid-form/dist/esm/index.d.ts
export * from './createForm.js';
// @filename: frontend/node_modules/@types/node/index.d.ts
export {};
// @filename: frontend/node_modules/solid-js/store/types/index.d.ts
export { $RAW, createStore, unwrap } from "./store.js";
// @filename: frontend/node_modules/solid-js/store/types/store.d.ts
export type Store<T> = T;
export type StoreReturn<T> = [get: Store<T>, set: SetStoreFunction<T>];
/**
 */
export declare function createStore<T extends object = {}>(...[store, options]: {} extends T ? [store?: T | Store<T>, options?: {
}] : [store: T | Store<T>, options?: {
}]): StoreReturn<T>;
// @filename: frontend/node_modules/vite/client.d.ts
export {};
// @filename: frontend/node_modules/zod/index.d.ts
export * from "./lib";
// @filename: frontend/node_modules/zod/lib/external.d.ts
export * from "./types";
// @filename: frontend/node_modules/zod/lib/helpers/util.d.ts
export declare namespace objectUtil {
    export type addQuestionMarks<T extends object, _O = any> = {
        [k in keyof T]?: unknown;
    };
}
// @filename: frontend/node_modules/zod/lib/index.d.ts
import * as z from "./external";
export { z };
// @filename: frontend/node_modules/zod/lib/types.d.ts
import { objectUtil, util } from "./helpers/util";
export declare type TypeOf<T extends ZodType<any, any, any>> = T["_output"];
export type { TypeOf as infer };
export declare abstract class ZodType<Output = any, Def extends ZodTypeDef = ZodTypeDef, Input = Output> {
    readonly _output: Output;
}
export declare type objectOutputType<Shape extends ZodRawShape, Catchall extends ZodTypeAny, UnknownKeys extends UnknownKeysParam = UnknownKeysParam> = objectUtil.flatten<objectUtil.addQuestionMarks<baseObjectOutputType<Shape>>> & CatchallOutput<Catchall> & PassthroughType<UnknownKeys>;
export declare type baseObjectOutputType<Shape extends ZodRawShape> = {
    [k in keyof Shape]: Shape[k]["_output"];
};
export declare type CatchallOutput<T extends ZodType> = ZodType extends T ? unknown : {
};
export declare type PassthroughType<T extends UnknownKeysParam> = T extends "passthrough" ? {
} : unknown;
export declare class ZodObject<T extends ZodRawShape, UnknownKeys extends UnknownKeysParam = UnknownKeysParam, Catchall extends ZodTypeAny = ZodTypeAny, Output = objectOutputType<T, Catchall, UnknownKeys>, Input = objectInputType<T, Catchall, UnknownKeys>> extends ZodType<Output, ZodObjectDef<T, UnknownKeys, Catchall>, Input> {
    strict(message?: errorUtil.ErrMessage): ZodObject<T, "strict", Catchall>;
}
declare const objectType: <T extends ZodRawShape>(shape: T, params?: RawCreateParams) => ZodObject<T, "strip", ZodTypeAny, { [k in keyof objectUtil.addQuestionMarks<baseObjectOutputType<T>, any>]: objectUtil.addQuestionMarks<baseObjectOutputType<T>, any>[k]; }, { [k_1 in keyof baseObjectInputType<T>]: baseObjectInputType<T>[k_1]; }>;
export { anyType as any, arrayType as array, bigIntType as bigint, booleanType as boolean, dateType as date, discriminatedUnionType as discriminatedUnion, effectsType as effect, enumType as enum, functionType as function, instanceOfType as instanceof, intersectionType as intersection, lazyType as lazy, literalType as literal, mapType as map, nanType as nan, nativeEnumType as nativeEnum, neverType as never, nullType as null, nullableType as nullable, numberType as number, objectType as object, oboolean, onumber, optionalType as optional, ostring, pipelineType as pipeline, preprocessType as preprocess, promiseType as promise, recordType as record, setType as set, strictObjectType as strictObject, stringType as string, symbolType as symbol, effectsType as transformer, tupleType as tuple, undefinedType as undefined, unionType as union, unknownType as unknown, voidType as void, };
// @filename: frontend/src/ts/components/pages/settings/SearchableAutoSetting.tsx
import { createForm } from "@tanstack/solid-form";
import { getConfig } from "../../../config/store";
export function SearchableAutoSetting<T extends ConfigKey>(props: {
  key: T;
}): JSXElement {
  const form = createForm(() => ({
    defaultValues: {
      [props.key]: getConfig[props.key],
    },
  }));
    if (
      z.ZodFirstPartyTypeKind.ZodNumber
    ) {
        <div class="grid w-full gap-2">
          <form
          >
            <form.Field
              name={props.key}
            />
          </form>
        </div>
    }
}
// @filename: frontend/src/ts/components/ui/form/utils.ts
// @filename: frontend/src/ts/config/store.ts
import type { Config as ConfigSchema } from "@monkeytype/schemas/configs";
import { createStore, reconcile } from "solid-js/store";
const [getConfigStore, setConfigStoreRaw] =
  createStore<ConfigSchema>(getDefaultConfig());
export const getConfig = getConfigStore;
// @filename: node_modules/@tanstack/form-core/dist/esm/FormApi.d.ts
export interface BaseFormOptions<in out TFormData, in out TSubmitMeta = never> {
    /**
     */
    defaultValues?: TFormData;
}
/**
 */
export interface FormOptions<in out TFormData, in out TOnMount extends undefined | FormValidateOrFn<TFormData>, in out TOnChange extends undefined | FormValidateOrFn<TFormData>, in out TOnChangeAsync extends undefined | FormAsyncValidateOrFn<TFormData>, in out TOnBlur extends undefined | FormValidateOrFn<TFormData>, in out TOnBlurAsync extends undefined | FormAsyncValidateOrFn<TFormData>, in out TOnSubmit extends undefined | FormValidateOrFn<TFormData>, in out TOnSubmitAsync extends undefined | FormAsyncValidateOrFn<TFormData>, in out TOnDynamic extends undefined | FormValidateOrFn<TFormData>, in out TOnDynamicAsync extends undefined | FormAsyncValidateOrFn<TFormData>, in out TOnServer extends undefined | FormAsyncValidateOrFn<TFormData>, in out TSubmitMeta = never> extends BaseFormOptions<TFormData, TSubmitMeta> {
}
/**
 */
export declare class FormApi<in out TFormData, in out TOnMount extends undefined | FormValidateOrFn<TFormData>, in out TOnChange extends undefined | FormValidateOrFn<TFormData>, in out TOnChangeAsync extends undefined | FormAsyncValidateOrFn<TFormData>, in out TOnBlur extends undefined | FormValidateOrFn<TFormData>, in out TOnBlurAsync extends undefined | FormAsyncValidateOrFn<TFormData>, in out TOnSubmit extends undefined | FormValidateOrFn<TFormData>, in out TOnSubmitAsync extends undefined | FormAsyncValidateOrFn<TFormData>, in out TOnDynamic extends undefined | FormValidateOrFn<TFormData>, in out TOnDynamicAsync extends undefined | FormAsyncValidateOrFn<TFormData>, in out TOnServer extends undefined | FormAsyncValidateOrFn<TFormData>, in out TSubmitMeta = never> implements FormLikeAPI<TFormData, TSubmitMeta> {
}
// @filename: node_modules/@tanstack/form-core/dist/esm/index.d.ts
export * from './FormApi.js';
export * from './util-types.js';
// @filename: node_modules/@tanstack/form-core/dist/esm/util-types.d.ts
export interface AnyDeepKeyAndValue<K extends string = string, V extends any = any> {
    value: V;
}
export type DeepKeyAndValueArray<TParent extends AnyDeepKeyAndValue, T extends ReadonlyArray<any>, TAcc> = DeepKeysAndValuesImpl<NonNullable<T[number]>, ArrayDeepKeyAndValue<TParent, T>, TAcc | ArrayDeepKeyAndValue<TParent, T>>;
export type AllTupleKeys<T> = T extends any ? keyof T & `${number}` : never;
export type DeepKeyAndValueTuple<TParent extends AnyDeepKeyAndValue, T extends ReadonlyArray<any>, TAcc, TAllKeys extends AllTupleKeys<T> = AllTupleKeys<T>> = TAllKeys extends any ? DeepKeysAndValuesImpl<NonNullable<T[TAllKeys]>, TupleDeepKeyAndValue<TParent, T, TAllKeys>, TAcc | TupleDeepKeyAndValue<TParent, T, TAllKeys>> : never;
export type AllObjectKeys<T> = T extends any ? keyof T & (string | number) : never;
export type DeepKeyAndValueObject<TParent extends AnyDeepKeyAndValue, T, TAcc, TAllKeys extends AllObjectKeys<T> = AllObjectKeys<T>> = TAllKeys extends any ? DeepKeysAndValuesImpl<NonNullable<T[TAllKeys]>, ObjectDeepKeyAndValue<TParent, T, TAllKeys>, TAcc | ObjectDeepKeyAndValue<TParent, T, TAllKeys>> : never;
export type DeepKeysAndValues<T> = DeepKeysAndValuesImpl<T> extends AnyDeepKeyAndValue ? DeepKeysAndValuesImpl<T> : never;
export type DeepKeysAndValuesImpl<T, TParent extends AnyDeepKeyAndValue = never, TAcc = never> = unknown extends T ? TAcc | UnknownDeepKeyAndValue<TParent> : unknown extends T ? T : T extends string | number | boolean | bigint | Date ? TAcc : T extends ReadonlyArray<any> ? number extends T['length'] ? DeepKeyAndValueArray<TParent, T, TAcc> : DeepKeyAndValueTuple<TParent, T, TAcc> : keyof T extends never ? TAcc | UnknownDeepKeyAndValue<TParent> : T extends object ? DeepKeyAndValueObject<TParent, T, TAcc> : TAcc;
export type DeepKeys<T> = unknown extends T ? string : DeepKeysAndValues<T>['key'];
/**
 */
