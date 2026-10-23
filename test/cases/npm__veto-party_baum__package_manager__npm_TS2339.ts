// origin: npm @veto-party/baum__package_manager__npm@0.2.0-pre-54 https://github.com/veto-party/baum tsconfig.bench.json seed 20261024
// flip: no
// @allowUmdGlobalAccess: true
// @emitDecoratorMetadata: true
// @experimentalDecorators: true
// @isolatedModules: true
// @lib: es2022, dom, dom.iterable, esnext, es6
// @module: nodenext
// @moduleResolution: nodenext
// @resolveJsonModule: true
// @skipLibCheck: true
// @strict: true
// @target: es2016
// @types: node
// @allowSyntheticDefaultImports: true
// @esModuleInterop: true
// @moduleDetection: force
// @preserveConstEnums: true
// @useDefineForClassFields: false
// @filename: node_modules/@types/lodash-es/package.json
{"name": "@types/lodash-es", "version": "4.17.12", "type": "module", "main": "", "types": "index.d.ts"}
// @filename: node_modules/@types/node/package.json
{"name": "@types/node", "version": "20.11.30", "main": "", "types": "index.d.ts"}
// @filename: node_modules/@veto-party/baum__steps__installer__features/package.json
{"name": "@veto-party/baum__steps__installer__features", "type": "module", "main": "./src/index.ts"}
// @filename: node_modules/ajv/package.json
{"name": "ajv", "version": "8.12.0", "main": "dist/ajv.js", "types": "dist/ajv.d.ts"}
// @filename: node_modules/json-schema-to-ts/package.json
{"name": "json-schema-to-ts", "version": "3.1.1", "main": "lib/cjs/index.js", "module": "lib/esm/index.js", "types": "lib/types/index.d.ts"}
// @filename: node_modules/ts-algebra/package.json
{"name": "ts-algebra", "version": "2.0.0", "main": "lib/index.js", "module": "lib/index.js", "types": "lib/index.d.ts"}
// @filename: package.json
{"name": "@veto-party/baum", "version": "1.0.0", "type": "module"}
// @filename: node_modules/@types/lodash-es/index.d.ts
export { default as cloneDeep } from "./cloneDeep.js";
export { default as get } from "./get.js";
export { default as isEqual } from "./isEqual.js";
export { default as merge } from "./merge.js";
export { default as set } from "./set.js";
export { default as toPath } from "./toPath.js";
// @filename: node_modules/@types/node/crypto.d.ts
/**
 */
declare module "crypto" {
    /**
     */
    function randomInt(max: number): number;
    /**
     */
}
declare module "node:crypto" {
    export * from "crypto";
}
// @filename: node_modules/@types/node/fs.d.ts
declare module "fs" {
    /**
     */
    export function readFileSync(
        options?:
            | BufferEncoding
    ): string | Buffer;
    export type WriteFileOptions =
        | null;
    /**
     */
    export function existsSync(path: PathLike): boolean;
}
declare module "node:fs" {
    export * from "fs";
}
// @filename: node_modules/@types/node/fs/promises.d.ts
declare module "fs/promises" {
    /**
     */
    function rm(path: PathLike, options?: RmOptions): Promise<void>;
    function stat(path: PathLike, opts?: StatOptions): Promise<Stats | BigIntStats>;
    /**
     */
    function writeFile(
        file: PathLike | FileHandle,
        data:
            | BufferEncoding
    ): Promise<void>;
    function appendFile(
        data: string | Uint8Array,
        options?: (ObjectEncodingOptions & FlagAndOpenMode & { flush?: boolean | undefined }) | BufferEncoding | null,
    ): Promise<void>;
    function readFile(
        options?:
            | BufferEncoding
    ): Promise<string | Buffer>;
}
declare module "node:fs/promises" {
    export * from "fs/promises";
}
// @filename: node_modules/@types/node/index.d.ts
/**
 */
/// <reference path="crypto.d.ts" />
/// <reference path="fs.d.ts" />
/// <reference path="fs/promises.d.ts" />
/// <reference path="path.d.ts" />
// @filename: node_modules/@types/node/path.d.ts
/**
 */
declare module "path" {
    namespace path {
        /**
         */
        interface PlatformPath {
            /**
             */
            join(...paths: string[]): string;
            /**
             */
            resolve(...paths: string[]): string;
            /**
             */
            isAbsolute(path: string): boolean;
            /**
             */
            readonly sep: "\\" | "/";
            /**
             */
        }
    }
    const path: path.PlatformPath;
    export = path;
}
declare module "node:path" {
    import path = require("path");
    export = path;
}
// @filename: node_modules/@veto-party/baum__steps__installer__features/src/abstract/AFeature.ts
import AJV from 'ajv';
import type { FromSchema } from 'json-schema-to-ts';
import { get } from 'lodash-es';
import type { IFeature, IngestResult } from '../interface/IFeature.js';
import type { FeatureContainer } from '../interface/IFeatureContainer.js';
import type { ToDefinitionStructure } from '../interface/types/ToDefinitionStructure.js';
export abstract class AFeature<T extends {} | Record<string, any>, Path, From = T extends {} ? any[] | any : FromSchema<T>> implements IFeature<T, Path, From> {
  private ajv = new AJV.default({
  });
  protected constructor(
    private schema: T,
    private lookup: Path extends string ? Path : undefined
  ) {}
  private getElementByLookupIfPresent(baseEl: any) {
    return this.lookup ? get(baseEl, this.lookup as string) : baseEl;
  }
  public getSchema(): T {
    return this.schema;
  }
  public getPath(): Path extends string ? Path : undefined {
    return this.lookup as any;
  }
  public verifyObject(element: any): element is ToDefinitionStructure<Path, From> {
    return this.ajv.validate(this.schema, this.getElementByLookupIfPresent(element));
  }
  public ingestObject(objects: Record<string, FeatureContainer>): Promise<IngestResult<From>[]> | IngestResult<From>[] {
    const items = Object.entries(objects).map(
      ([key, element]) =>
        ({
          item: this.getElementByLookupIfPresent(element.feature),
        }) satisfies IngestResult<From>
    );
    return items;
  }
}
// @filename: node_modules/@veto-party/baum__steps__installer__features/src/abstract/GroupFeature/GroupFeature.ts
import type { asConst, FromSchema, JSONSchema } from 'json-schema-to-ts';
import { cloneDeep, set, toPath } from 'lodash-es';
import type { IFeature } from '../../interface/IFeature.js';
import { AFeature } from '../AFeature.js';
import type { ResolveFeaturePath } from '../types/MergeFeatures.js';
import type { ToDefinitionStructureWithTupleMerge } from '../types/ToDefinitionStructureWithTupleMerge.js';
export class GroupFeature<T extends {} | Record<string, any>, Path, From = T extends {} ? any[] | any : FromSchema<T>> extends AFeature<T, Path, From> {
  protected do_construct(newSchema: any, path: string | undefined): GroupFeature<any, Path> {
    return new GroupFeature<any, Path>(newSchema, path as any);
  }
  appendFeature<WritePath, Feature extends IFeature<any, any, any>>(
    writePath: WritePath,
    feature: Feature
  ): Feature extends IFeature<infer D, infer B, infer _>
    ? ReturnType<typeof asConst<ToDefinitionStructureWithTupleMerge<ResolveFeaturePath<WritePath, B extends string | undefined ? B : undefined>, D, T>>> extends infer Some
      ? GroupFeature<Some extends Record<string, any> ? Some : never, Path, FromSchema<Some extends JSONSchema ? Some : never>>
      : never
    : never {
    const oldSchemaCloned = cloneDeep(this.getSchema());
    return this.do_construct(oldSchemaCloned, this.getPath()) as any;
  }
}
// @filename: node_modules/@veto-party/baum__steps__installer__features/src/abstract/types/MergeDeepForToDefinitionStructureWithTupleMerge.ts
const MERGE_ELEM_NOT_FOUND = Symbol('MERGE_ELEM_NOT_FOUND');
type MergeTuple<A extends any[], B extends any[]> = DoMergeTuple<A, A, B, B, []>;
type DoMergeTuple<A extends any[], AOriginal extends any[], B extends any[], BOriginal extends any[], Items extends any[]> = AOriginal['length'] extends Items['length']
  ? BOriginal extends Items['length']
    ? Items
    : [...Items, ...B]
  : BOriginal['length'] extends Items['length']
    ? AOriginal['length'] extends Items['length']
      ? Items
      : [...Items, ...A]
    : A extends [infer CurrentA, ...infer RestA]
      ? B extends [infer CurrentB, ...infer RestB]
        ? DoMergeTuple<RestA, AOriginal, RestB, BOriginal, [...Items, MergeDeepForToDefinitionStructureWithTupleMerge<CurrentA, CurrentB>]>
        : B extends [infer CurrentB]
          ? DoMergeTuple<RestA, AOriginal, [], BOriginal, [...Items, MergeDeepForToDefinitionStructureWithTupleMerge<CurrentA, CurrentB>]>
          : DoMergeTuple<RestA, AOriginal, [], BOriginal, [...Items, CurrentA]>
      : A extends [infer CurrentA]
        ? B extends [infer CurrentB, ...infer RestB]
          ? DoMergeTuple<[], AOriginal, RestB, BOriginal, [...Items, MergeDeepForToDefinitionStructureWithTupleMerge<CurrentA, CurrentB>]>
          : B extends [infer CurrentB]
            ? DoMergeTuple<[], AOriginal, [], BOriginal, [...Items, MergeDeepForToDefinitionStructureWithTupleMerge<CurrentA, CurrentB>]>
            : DoMergeTuple<[], AOriginal, [], BOriginal, [...Items, CurrentA]>
        : B extends [infer CurrentB, ...infer RestB]
          ? A extends [infer CurrentA, ...infer RestA]
            ? DoMergeTuple<RestA, AOriginal, RestB, BOriginal, [...Items, MergeDeepForToDefinitionStructureWithTupleMerge<CurrentA, CurrentB>]>
            : A extends [infer CurrentA]
              ? DoMergeTuple<[], AOriginal, RestB, BOriginal, [...Items, MergeDeepForToDefinitionStructureWithTupleMerge<CurrentA, CurrentB>]>
              : DoMergeTuple<[], AOriginal, RestB, BOriginal, [...Items, CurrentB]>
          : B extends [infer CurrentB]
            ? A extends [infer CurrentA, ...infer RestA]
              ? DoMergeTuple<RestA, AOriginal, [], BOriginal, [...Items, MergeDeepForToDefinitionStructureWithTupleMerge<CurrentA, CurrentB>]>
              : A extends [infer CurrentA]
                ? DoMergeTuple<[], AOriginal, [], BOriginal, [...Items, MergeDeepForToDefinitionStructureWithTupleMerge<CurrentA, CurrentB>]>
                : DoMergeTuple<[], AOriginal, [], BOriginal, [...Items, CurrentB]>
            : never;
/**
 */
export type MergeDeepForToDefinitionStructureWithTupleMerge<A, B> = A extends typeof MERGE_ELEM_NOT_FOUND
  ? B
  : B extends typeof MERGE_ELEM_NOT_FOUND
    ? A
    : A extends any[]
      ? B extends any[]
        ? MergeTuple<A, B>
        : A
      : B extends any[]
        ? A extends any[]
          ? MergeTuple<A, B>
          : B
        : A extends object
          ? B extends object
            ? { [Key in keyof (A & B)]: MergeDeepForToDefinitionStructureWithTupleMerge<Key extends keyof A ? A[Key] : typeof MERGE_ELEM_NOT_FOUND, Key extends keyof B ? B[Key] : typeof MERGE_ELEM_NOT_FOUND> }
            : A
          : B extends object
            ? A extends object
              ? { [Key in keyof (A & B)]: MergeDeepForToDefinitionStructureWithTupleMerge<Key extends keyof A ? A[Key] : typeof MERGE_ELEM_NOT_FOUND, Key extends keyof B ? B[Key] : typeof MERGE_ELEM_NOT_FOUND> }
              : B
            : A | B;
// @filename: node_modules/@veto-party/baum__steps__installer__features/src/abstract/types/MergeFeatures.ts
import type { FromSchema, JSONSchema } from 'json-schema-to-ts';
import type { IFeature } from '../../interface/IFeature.js';
import type { ToDefinitionStructureWithTupleMerge } from './ToDefinitionStructureWithTupleMerge.js';
export type ResolveFeaturePath<WritePath, B extends string | undefined> = WritePath extends string ? (B extends undefined ? WritePath : `${WritePath}.${B}`) : B;
export type MergeFeatures<A extends IFeature<any, any, any>, Path, B extends IFeature<any, any, any>> = A extends IFeature<infer TypeA, infer PathA, any>
  ? B extends IFeature<infer TypeB, infer PathB, any>
    ? PathB extends string | undefined
      ? ToDefinitionStructureWithTupleMerge<ResolveFeaturePath<Path, PathB>, TypeB, TypeA> extends infer NewType
        ? NewType extends JSONSchema
          ? IFeature<NewType, PathA, FromSchema<NewType>>
          : never
        : never
      : never
    : never
  : never;
// @filename: node_modules/@veto-party/baum__steps__installer__features/src/abstract/types/ToDefinitionStructureWithTupleMerge.ts
import type { asConst } from 'json-schema-to-ts';
import type { ToDefinitionStructure } from '../../interface/types/ToDefinitionStructure.js';
import type { MergeDeepForToDefinitionStructureWithTupleMerge } from './MergeDeepForToDefinitionStructureWithTupleMerge.js';
export type ToDefinitionStructureWithTupleMerge<Path, Target, Obj> = ToDefinitionStructure<Path, Target> extends infer U ? (MergeDeepForToDefinitionStructureWithTupleMerge<Obj, U> extends infer R ? ReturnType<typeof asConst<R>> : never) : never;
// @filename: node_modules/@veto-party/baum__steps__installer__features/src/implementation/Variable/Variable.ts
import type { FromSchema } from 'json-schema-to-ts';
import { GroupFeature } from '../../abstract/GroupFeature/GroupFeature.js';
import { definition, variableAccessorPattern } from './definition.js';
export class VariableFeature<T extends typeof definition = typeof definition> extends GroupFeature<T, 'variable', FromSchema<T>> {
  protected constructor(value: any) {
    super(value, 'variable');
  }
  public static makeInstance() {
    const result = new VariableFeature(definition) //.appendFeature('patternProperties.["^[a-zA-Z0-9_]*$"]', new BaseVariableFeature());
    return result;
  }
}
// @filename: node_modules/@veto-party/baum__steps__installer__features/src/implementation/Variable/definition.ts
import { asConst } from 'json-schema-to-ts';
export const variableAccessorPattern = '^[a-zA-Z0-9_]*$' as const;
export const definition = asConst({
  type: 'object',
  patternProperties: {
    [variableAccessorPattern]: {
      type: 'object',
    }
  },
});
// @filename: node_modules/@veto-party/baum__steps__installer__features/src/index.ts
export { VariableFeature } from './implementation/Variable/Variable.js';
export type { IFeature, IngestResult } from './interface/IFeature.js';
// @filename: node_modules/@veto-party/baum__steps__installer__features/src/interface/IFeature.ts
import type { FromSchema } from 'json-schema-to-ts';
import type { FeatureContainer } from './IFeatureContainer.js';
import type { ToDefinitionStructure } from './types/ToDefinitionStructure.js';
export type IngestResult<T> = {
  item: T;
};
export interface IFeature<T extends Record<string, any> | {}, Path, From = T extends {} ? any[] | any : FromSchema<T>> {
  /**
   */
  verifyObject(element: any): element is ToDefinitionStructure<Path, From>;
  getSchema(): T;
  getPath(): Path extends string ? Path : undefined;
  ingestObject(objects: Record<string, FeatureContainer>): Promise<IngestResult<From>[]> | IngestResult<From>[];
}
// @filename: node_modules/@veto-party/baum__steps__installer__features/src/interface/IFeatureContainer.ts
export interface FeatureContainer {
  feature: any;
}
// @filename: node_modules/@veto-party/baum__steps__installer__features/src/interface/types/KeyValuePair.ts
export type KeyValuePair<A extends string | unknown, Target> = A extends string | number | symbol ? { [key in A]: Target } : Target;
// @filename: node_modules/@veto-party/baum__steps__installer__features/src/interface/types/MakeTuple.ts
export type MakeTuple<T, N extends number = 1, Items extends any[] = []> = N extends Items['length'] ? [...Items, T] : MakeTuple<T, N, [void, ...Items]>;
// @filename: node_modules/@veto-party/baum__steps__installer__features/src/interface/types/StringToNumber.ts
export type StringToNumber<S extends string | number, AlternativeBase = never, Alternative extends AlternativeBase = never> = S extends number
  ? S
                      : Alternative;
// @filename: node_modules/@veto-party/baum__steps__installer__features/src/interface/types/ToDefinitionStructure.ts
import type { KeyValuePair } from './KeyValuePair.js';
import type { MakeTuple } from './MakeTuple.js';
import type { StringToNumber } from './StringToNumber.js';
export type ToDefinitionStructure<Path, Target> = Path extends string
  ? // No path is given, return target
    Path extends undefined
    ? Target
            : Path extends `${infer U}[${infer index}]`
              ? ToDefinitionStructure<U, MakeTuple<Target, StringToNumber<index>>>
                    : Path extends `[${infer index}].${infer Rest}`
                      ? MakeTuple<ToDefinitionStructure<Rest, Target>, StringToNumber<index>>
                      : Path extends `${infer U}.${infer Rest}`
                        ? ToDefinitionStructure<U, ToDefinitionStructure<Rest, Target>>
                        : KeyValuePair<Path, Target>
  : Target;
// @filename: node_modules/ajv/dist/ajv.d.ts
export default Ajv;
// @filename: node_modules/json-schema-to-ts/lib/types/definitions/index.d.ts
export type { JSONSchema, JSONSchemaType, JSONSchemaReference, } from "./jsonSchema";
// @filename: node_modules/json-schema-to-ts/lib/types/definitions/jsonSchema.d.ts
export type JSONSchema = boolean | Readonly<{
}>;
// @filename: node_modules/json-schema-to-ts/lib/types/index.d.ts
import type { M } from "ts-algebra";
import type { ParseOptions } from "./parse-options";
import type { ParseSchema } from "./parse-schema";
export type { ExtendedJSONSchema, DeserializationPattern, FromSchemaOptions, FromExtendedSchemaOptions, FromSchemaDefaultOptions, JSONSchemaExtension, JSONSchema, } from "./definitions";
export { wrapCompilerAsTypeGuard, wrapValidatorAsTypeGuard, asConst, } from "./utils";
export type FromSchema<SCHEMA extends JSONSchema, OPTIONS extends FromSchemaOptions = FromSchemaDefaultOptions> = M.$Resolve<ParseSchema<SCHEMA, ParseOptions<SCHEMA, OPTIONS>>>;
// @filename: node_modules/json-schema-to-ts/lib/types/parse-options.d.ts
export type ParseOptions<ROOT_SCHEMA extends JSONSchema, OPTIONS extends FromSchemaOptions> = {
};
// @filename: node_modules/json-schema-to-ts/lib/types/parse-schema/allOf.d.ts
import type { JSONSchema } from "../definitions";
export type AllOfSchema = JSONSchema & Readonly<{
    allOf: readonly JSONSchema[];
}>;
// @filename: node_modules/json-schema-to-ts/lib/types/parse-schema/anyOf.d.ts
import type { JSONSchema } from "../definitions";
export type AnyOfSchema = JSONSchema & Readonly<{
    anyOf: readonly JSONSchema[];
}>;
// @filename: node_modules/json-schema-to-ts/lib/types/parse-schema/array.d.ts
import type { JSONSchema } from "../definitions";
export type ArrayOrTupleSchema = JSONSchema & Readonly<{
    type: "array";
}>;
// @filename: node_modules/json-schema-to-ts/lib/types/parse-schema/const.d.ts
import type { JSONSchema } from "../definitions";
export type ConstSchema = JSONSchema & Readonly<{
    const: unknown;
}>;
// @filename: node_modules/json-schema-to-ts/lib/types/parse-schema/enum.d.ts
import type { JSONSchema } from "../definitions";
export type EnumSchema = JSONSchema & Readonly<{
    enum: readonly unknown[];
}>;
// @filename: node_modules/json-schema-to-ts/lib/types/parse-schema/ifThenElse.d.ts
import type { JSONSchema } from "../definitions";
export type IfThenElseSchema = JSONSchema & {
    if: JSONSchema;
};
// @filename: node_modules/json-schema-to-ts/lib/types/parse-schema/index.d.ts
import type { M } from "ts-algebra";
import type { DeserializationPattern, JSONSchema } from "../definitions";
import type { AllOfSchema, ParseAllOfSchema } from "./allOf";
import type { AnyOfSchema, ParseAnyOfSchema } from "./anyOf";
import type { ConstSchema, ParseConstSchema } from "./const";
import type { EnumSchema, ParseEnumSchema } from "./enum";
import type { IfThenElseSchema, ParseIfThenElseSchema } from "./ifThenElse";
import type { MultipleTypesSchema, ParseMultipleTypesSchema } from "./multipleTypes";
import type { NotSchema, ParseNotSchema } from "./not";
import type { NullableSchema, ParseNullableSchema } from "./nullable";
import type { OneOfSchema, ParseOneOfSchema } from "./oneOf";
import type { ParseReferenceSchema, ReferencingSchema } from "./references";
import type { ParseSingleTypeSchema, SingleTypeSchema } from "./singleType";
export type ParseSchema<SCHEMA extends JSONSchema, OPTIONS extends ParseSchemaOptions, RESULT = JSONSchema extends SCHEMA ? M.Any : SCHEMA extends true | string ? M.Any : SCHEMA extends false ? M.Never : SCHEMA extends NullableSchema ? ParseNullableSchema<SCHEMA, OPTIONS> : SCHEMA extends ReferencingSchema ? ParseReferenceSchema<SCHEMA, OPTIONS> : And<DoesExtend<OPTIONS["parseIfThenElseKeywords"], true>, DoesExtend<SCHEMA, IfThenElseSchema>> extends true ? SCHEMA extends IfThenElseSchema ? ParseIfThenElseSchema<SCHEMA, OPTIONS> : never : And<DoesExtend<OPTIONS["parseNotKeyword"], true>, DoesExtend<SCHEMA, NotSchema>> extends true ? SCHEMA extends NotSchema ? ParseNotSchema<SCHEMA, OPTIONS> : never : SCHEMA extends AllOfSchema ? ParseAllOfSchema<SCHEMA, OPTIONS> : SCHEMA extends OneOfSchema ? ParseOneOfSchema<SCHEMA, OPTIONS> : SCHEMA extends AnyOfSchema ? ParseAnyOfSchema<SCHEMA, OPTIONS> : SCHEMA extends EnumSchema ? ParseEnumSchema<SCHEMA, OPTIONS> : SCHEMA extends ConstSchema ? ParseConstSchema<SCHEMA, OPTIONS> : SCHEMA extends MultipleTypesSchema ? ParseMultipleTypesSchema<SCHEMA, OPTIONS> : SCHEMA extends SingleTypeSchema ? ParseSingleTypeSchema<SCHEMA, OPTIONS> : M.Any> = OPTIONS extends {
    deserialize: DeserializationPattern[];
} ? M.$Intersect<DeserializeSchema<SCHEMA, OPTIONS>, RESULT> : RESULT;
// @filename: node_modules/json-schema-to-ts/lib/types/parse-schema/multipleTypes.d.ts
import type { JSONSchema, JSONSchemaType } from "../definitions";
export type MultipleTypesSchema = JSONSchema & Readonly<{
    type: readonly JSONSchemaType[];
}>;
// @filename: node_modules/json-schema-to-ts/lib/types/parse-schema/not.d.ts
import type { JSONSchema } from "../definitions";
export type NotSchema = JSONSchema & Readonly<{
    not: JSONSchema;
}>;
// @filename: node_modules/json-schema-to-ts/lib/types/parse-schema/nullable.d.ts
import type { JSONSchema } from "../definitions";
export type NullableSchema = JSONSchema & Readonly<{
    nullable: boolean;
}>;
// @filename: node_modules/json-schema-to-ts/lib/types/parse-schema/object.d.ts
import type { M } from "ts-algebra";
import type { ParseSchema, ParseSchemaOptions } from "./index";
export type ParseObjectSchema<OBJECT_SCHEMA extends ObjectSchema, OPTIONS extends ParseSchemaOptions> = OBJECT_SCHEMA extends Readonly<{
}> ? M.$Object<{
}, GetRequired<OBJECT_SCHEMA, OPTIONS>, GetOpenProps<OBJECT_SCHEMA, OPTIONS>, GetClosedOnResolve<OBJECT_SCHEMA>> : M.$Object<{}, GetRequired<OBJECT_SCHEMA, OPTIONS>, GetOpenProps<OBJECT_SCHEMA, OPTIONS>, GetClosedOnResolve<OBJECT_SCHEMA>>;
type GetRequired<OBJECT_SCHEMA extends ObjectSchema, OPTIONS extends ParseSchemaOptions> = (OBJECT_SCHEMA extends Readonly<{
}> ? {
}[keyof OBJECT_SCHEMA["properties"] & string] : never);
type GetOpenProps<OBJECT_SCHEMA extends ObjectSchema, OPTIONS extends ParseSchemaOptions> = OBJECT_SCHEMA extends Readonly<{
    additionalProperties: JSONSchema;
}> ? OBJECT_SCHEMA extends Readonly<{
}> ? AdditionalAndPatternProps<OBJECT_SCHEMA["additionalProperties"], OBJECT_SCHEMA["patternProperties"], OPTIONS> : ParseSchema<OBJECT_SCHEMA["additionalProperties"], OPTIONS> : OBJECT_SCHEMA extends Readonly<{
    patternProperties: Record<string, JSONSchema>;
}> ? PatternProps<OBJECT_SCHEMA["patternProperties"], OPTIONS> : M.Any;
type GetClosedOnResolve<OBJECT_SCHEMA extends ObjectSchema> = OBJECT_SCHEMA extends Readonly<{
    unevaluatedProperties: false;
}> ? true : false;
type PatternProps<PATTERN_PROPERTY_SCHEMAS extends Readonly<Record<string, JSONSchema>>, OPTIONS extends ParseSchemaOptions> = M.$Union<{
    [KEY in keyof PATTERN_PROPERTY_SCHEMAS]: ParseSchema<PATTERN_PROPERTY_SCHEMAS[KEY], OPTIONS>;
}[keyof PATTERN_PROPERTY_SCHEMAS]>;
// @filename: node_modules/json-schema-to-ts/lib/types/parse-schema/oneOf.d.ts
import type { JSONSchema } from "../definitions";
export type OneOfSchema = JSONSchema & Readonly<{
    oneOf: readonly JSONSchema[];
}>;
// @filename: node_modules/json-schema-to-ts/lib/types/parse-schema/references/index.d.ts
import type { JSONSchema } from "../../definitions";
export type ReferencingSchema = JSONSchema & {
    $ref: string;
};
// @filename: node_modules/json-schema-to-ts/lib/types/parse-schema/singleType.d.ts
import type { ArrayOrTupleSchema, ParseArrayOrTupleSchema } from "./array";
import type { ObjectSchema, ParseObjectSchema } from "./object";
export type ParseSingleTypeSchema<SINGLE_TYPE_SCHEMA extends SingleTypeSchema, OPTIONS extends ParseSchemaOptions> = SINGLE_TYPE_SCHEMA extends Readonly<{
    type: "string";
}> ? M.Primitive<string> : SINGLE_TYPE_SCHEMA extends ArrayOrTupleSchema ? ParseArrayOrTupleSchema<SINGLE_TYPE_SCHEMA, OPTIONS> : SINGLE_TYPE_SCHEMA extends ObjectSchema ? ParseObjectSchema<SINGLE_TYPE_SCHEMA, OPTIONS> : M.Never;
// @filename: node_modules/json-schema-to-ts/lib/types/type-utils/index.d.ts
export type { Narrow } from "./narrow";
// @filename: node_modules/json-schema-to-ts/lib/types/type-utils/narrow.d.ts
export type Narrow<INPUT> = INPUT extends Promise<infer AWAITED> ? Promise<Narrow<AWAITED>> : INPUT extends (...args: infer ARGS) => infer RETURN ? (...args: Narrow<ARGS>) => Narrow<RETURN> : INPUT extends [] ? [] : INPUT extends object ? {
    [KEY in keyof INPUT]: Narrow<INPUT[KEY]>;
} : INPUT extends string | number | boolean | bigint ? INPUT : never;
// @filename: node_modules/json-schema-to-ts/lib/types/utils/asConst.d.ts
import type { Narrow } from "../type-utils";
export declare const asConst: <INPUT>(input: Narrow<INPUT>) => Narrow<INPUT>;
// @filename: node_modules/json-schema-to-ts/lib/types/utils/index.d.ts
export { asConst } from "./asConst";
// @filename: node_modules/ts-algebra/lib/index.d.ts
export * as M from "./meta-types";
// @filename: node_modules/ts-algebra/lib/meta-types/any.d.ts
export declare type Any<IS_SERIALIZED extends boolean = false, DESERIALIZED = never> = {
    deserialized: DESERIALIZED;
};
export declare type AnyType = {
    isSerialized: boolean;
};
// @filename: node_modules/ts-algebra/lib/meta-types/array.d.ts
export declare type ArrayType = {
    type: ArrayTypeId;
    deserialized: unknown;
};
// @filename: node_modules/ts-algebra/lib/meta-types/const.d.ts
export declare type ConstType = {
    isSerialized: boolean;
};
// @filename: node_modules/ts-algebra/lib/meta-types/enum.d.ts
export declare type EnumType = {
    isSerialized: boolean;
};
// @filename: node_modules/ts-algebra/lib/meta-types/index.d.ts
import type { Any, AnyType } from "./any";
import type { _$Object, _Object, ObjectType } from "./object";
import type { $Resolve, Resolve } from "./resolve";
import type { $Union, Union, UnionType } from "./union";
export type { Never, Any, Const, Enum, Primitive, $Primitive, _Array as Array, _$Array as $Array, Tuple, $Tuple, _Object as Object, _$Object as $Object, Union, $Union, NeverType, AnyType, ConstType, EnumType, PrimitiveType, ArrayType, TupleType, ObjectType, UnionType, Type, $Resolve, Resolve, $Intersect, Intersect, _$Exclude as $Exclude, _Exclude as Exclude, };
// @filename: node_modules/ts-algebra/lib/meta-types/never.d.ts
export declare type Never = {
    type: NeverTypeId;
};
export declare type NeverType = Never;
export declare type ResolveNever = never;
// @filename: node_modules/ts-algebra/lib/meta-types/object.d.ts
import type { And, DeepMergeUnsafe, DoesExtend, If, IsNever, Not } from "../utils";
import type { Never, NeverType } from "./never";
import type { Resolve, ResolveOptions } from "./resolve";
import type { Deserialized, IsSerialized } from "./utils";
export declare type _$Object<VALUES = {}, REQUIRED_KEYS = never, OPEN_PROPS = Never, CLOSE_ON_RESOLVE = false, IS_SERIALIZED = false, DESERIALIZED = never> = DoesExtend<true, {
}[Extract<REQUIRED_KEYS, string>]> extends true ? Never : {
    isOpen: Not<DoesExtend<OPEN_PROPS, NeverType>>;
    openProps: OPEN_PROPS;
    closeOnResolve: CLOSE_ON_RESOLVE;
    deserialized: DESERIALIZED;
};
export declare type ObjectType = {
    deserialized: unknown;
};
export declare type IsObjectOpen<META_OBJECT extends ObjectType> = META_OBJECT["isOpen"];
export declare type ObjectOpenProps<META_OBJECT extends ObjectType> = META_OBJECT["openProps"];
export declare type IsObjectClosedOnResolve<META_OBJECT extends ObjectType> = META_OBJECT["closeOnResolve"];
export declare type ResolveObject<META_OBJECT extends ObjectType, OPTIONS extends ResolveOptions> = If<And<OPTIONS["deserialize"], IsSerialized<META_OBJECT>>, Deserialized<META_OBJECT>, DeepMergeUnsafe<If<And<IsObjectOpen<META_OBJECT>, Not<IsObjectClosedOnResolve<META_OBJECT>>>, If<IsObjectEmpty<META_OBJECT>, {
    [KEY: string]: Resolve<ObjectOpenProps<META_OBJECT>, OPTIONS>;
}>, {}>, DeepMergeUnsafe<{
}, {
}>>>;
// @filename: node_modules/ts-algebra/lib/meta-types/primitive.d.ts
export declare type PrimitiveType = {
    isSerialized: boolean;
};
// @filename: node_modules/ts-algebra/lib/meta-types/resolve.d.ts
import type { AnyType, ResolveAny } from "./any";
import type { ArrayType, ResolveArray } from "./array";
import type { ConstType, ResolveConst } from "./const";
import type { EnumType, ResolveEnum } from "./enum";
import type { NeverType, ResolveNever } from "./never";
import type { ObjectType, ResolveObject } from "./object";
import type { PrimitiveType, ResolvePrimitive } from "./primitive";
import type { ResolveTuple, TupleType } from "./tuple";
import type { ResolveUnion, UnionType } from "./union";
export declare type Resolve<META_TYPE extends Type, OPTIONS extends ResolveOptions = ResolveDefaultOptions> = $Resolve<META_TYPE, OPTIONS>;
export declare type $Resolve<META_TYPE, OPTIONS extends ResolveOptions = ResolveDefaultOptions> = META_TYPE extends AnyType ? ResolveAny<META_TYPE, OPTIONS> : META_TYPE extends NeverType ? ResolveNever : META_TYPE extends ConstType ? ResolveConst<META_TYPE, OPTIONS> : META_TYPE extends EnumType ? ResolveEnum<META_TYPE, OPTIONS> : META_TYPE extends PrimitiveType ? ResolvePrimitive<META_TYPE, OPTIONS> : META_TYPE extends ArrayType ? ResolveArray<META_TYPE, OPTIONS> : META_TYPE extends TupleType ? ResolveTuple<META_TYPE, OPTIONS> : META_TYPE extends ObjectType ? ResolveObject<META_TYPE, OPTIONS> : META_TYPE extends UnionType ? ResolveUnion<META_TYPE, OPTIONS> : never;
// @filename: node_modules/ts-algebra/lib/meta-types/tuple.d.ts
export declare type TupleType = {
    type: TupleTypeId;
    deserialized: unknown;
};
// @filename: node_modules/ts-algebra/lib/meta-types/union.d.ts
import type { DoesExtend, If, IsNever } from "../utils";
import type { Never, NeverType } from "./never";
import type { $Resolve, ResolveOptions } from "./resolve";
export declare type $Union<VALUES> = If<IsNever<VALUES>, Never, DoesExtend<VALUES, NeverType> extends true ? Never : {
    values: VALUES;
}>;
export declare type UnionValues<META_UNION extends UnionType> = META_UNION["values"];
export declare type ResolveUnion<META_UNION extends UnionType, OPTIONS extends ResolveOptions> = RecurseOnUnion<UnionValues<META_UNION>, OPTIONS>;
declare type RecurseOnUnion<VALUES extends Type, OPTIONS extends ResolveOptions> = VALUES extends infer META_TYPE ? $Resolve<META_TYPE, OPTIONS> : never;
// @filename: node_modules/ts-algebra/lib/meta-types/utils.d.ts
export declare type Deserialized<SERIALIZABLE_META_TYPE extends SerializableType> = SERIALIZABLE_META_TYPE["deserialized"];
// @filename: node_modules/ts-algebra/lib/utils/and.d.ts
export declare type And<BOOL_A, BOOL_B> = BOOL_A extends true ? BOOL_B extends true ? true : false : false;
// @filename: node_modules/ts-algebra/lib/utils/extends.d.ts
export declare type DoesExtend<TYPE_A, TYPE_B> = [TYPE_A] extends [TYPE_B] ? true : false;
declare type ArrayKeys = keyof [];
export declare type IsObject<TYPE> = TYPE extends object ? ArrayKeys extends Extract<keyof TYPE, ArrayKeys> ? false : true : false;
// @filename: node_modules/ts-algebra/lib/utils/if.d.ts
export declare type If<CONDITION extends boolean, THEN, ELSE = never> = CONDITION extends true ? THEN : ELSE;
// @filename: node_modules/ts-algebra/lib/utils/index.d.ts
export type { And } from "./and";
export type { DoesExtend, IsObject, IsArray } from "./extends";
export type { If } from "./if";
export type { DeepMergeUnsafe } from "./merge";
export type { Not } from "./not";
// @filename: node_modules/ts-algebra/lib/utils/merge.d.ts
import type { IsArray, IsObject } from "./extends";
export declare type DeepMergeUnsafe<TYPE_A, TYPE_B> = IsObject<TYPE_A> extends true ? IsObject<TYPE_B> extends true ? {
    [KEY in keyof (TYPE_A & TYPE_B)]: KEY extends keyof TYPE_B ? KEY extends keyof TYPE_A ? DeepMergeUnsafe<TYPE_A[KEY], TYPE_B[KEY]> : TYPE_B[KEY] : KEY extends keyof TYPE_A ? TYPE_A[KEY] : never;
} : TYPE_B : IsArray<TYPE_A> extends true ? IsArray<TYPE_B> extends true ? TYPE_B extends unknown[] ? [
] : never : TYPE_B : TYPE_B;
// @filename: node_modules/ts-algebra/lib/utils/not.d.ts
export declare type Not<BOOL extends boolean> = BOOL extends false ? true : BOOL extends true ? false : never;
// @filename: steps/installer/renderer/src/implementation/helm/implementation/Secret/SecretRenderer.ts
import { extractVariables } from '../../../utility/extractVariables.js';
declare const workspace: any, map: any, binding: any;
    const allItems = new Map(Array.from(extractVariables(workspace, map, binding).entries()).filter(([, value]) => value.secret === true));
// @filename: steps/installer/renderer/src/implementation/helm/interface/factory/IConfigMapRenderer.ts
import type { IFeature, VariableFeature } from '@veto-party/baum__steps__installer__features';
export type IConfigMapStructure = typeof VariableFeature.makeInstance extends () => IFeature<any, any, infer Structure> ? (Structure extends Record<infer Key, infer Value> ? Map<Key, Value> : never) : never;
// @filename: steps/installer/renderer/src/implementation/utility/extractVariables.ts
import type { IConfigMapStructure } from '../helm/interface/factory/IConfigMapRenderer.js';
type IWorkspace = {};
class VariableExtractor {
  extractVariables(workspace: IWorkspace | undefined, map: Map<IWorkspace | undefined, IConfigMapStructure>, binding: Map<string, string> | undefined) {
    const allItems: IConfigMapStructure extends Map<infer Key, infer Value> ? Map<Key, Value & { source: string }> : never = new Map();
    return allItems;
  }
}
const extractor = new VariableExtractor();
export const extractVariables = extractor.extractVariables.bind(extractor);
