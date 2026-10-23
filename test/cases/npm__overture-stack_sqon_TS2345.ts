// origin: npm @overture-stack/sqon@1.0.0-rc.1 https://github.com/overture-stack/arranger modules/sqon/tsconfig.bench.json seed 20261023
// flip: no
// An array literal of as-const objects against SqonNode | SqonNode[] (zod v4 z.infer members): tsgo reports TS2345 on the argument, tttt elaborates TS2322 into an element
// @lib: es2024, es2026.array, es2026.collection, esnext.iterator
// @module: nodenext
// @moduleResolution: nodenext
// @noImplicitAny: true
// @noImplicitReturns: true
// @noUncheckedIndexedAccess: true
// (paths is an object: not expressible as a header) {"#*": ["./src/*"]}
// @skipLibCheck: true
// @strict: true
// @target: es2022
// @types: node
// @verbatimModuleSyntax: true
// @esModuleInterop: true
// @moduleDetection: force
// @isolatedModules: true
// @preserveConstEnums: true
// @filename: modules/sqon/package.json
{"name": "@overture-stack/sqon", "version": "0.0.0-dev", "main": "dist/index.cjs", "types": "dist/index.d.ts", "exports": {".": {"import": "./dist/index.js", "require": "./dist/index.cjs", "types": "./dist/index.d.ts"}}, "imports": {"#*": "./src/*"}, "type": "module"}
// @filename: node_modules/@types/node/package.json
{"name": "@types/node", "version": "22.19.19", "main": "", "types": "index.d.ts", "typesVersions": {"<=5.6": {"*": ["ts5.6/*"]}}}
// @filename: node_modules/zod/package.json
{"name": "zod", "version": "4.5.4", "type": "module", "main": "./index.cjs", "types": "./index.d.cts", "module": "./index.js", "exports": {"./package.json": "./package.json", ".": {"@zod/source": "./src/index.ts", "types": "./index.d.cts", "import": "./index.js", "require": "./index.cjs"}, "./mini": {"@zod/source": "./src/mini/index.ts", "types": "./mini/index.d.cts", "import": "./mini/index.js", "require": "./mini/index.cjs"}, "./compile": {"@zod/source": "./src/compile.ts", "types": "./compile.d.cts", "import": "./compile.js", "require": "./compile.cjs"}, "./locales": {"@zod/source": "./src/locales/index.ts", "types": "./locales/index.d.cts", "import": "./locales/index.js", "require": "./locales/index.cjs"}, "./v3": {"@zod/source": "./src/v3/index.ts", "types": "./v3/index.d.cts", "import": "./v3/index.js", "require": "./v3/index.cjs"}, "./v4": {"@zod/source": "./src/v4/index.ts", "types": "./v4/index.d.cts", "import": "./v4/index.js", "require": "./v4/index.cjs"}, "./v4-mini": {"@zod/source": "./src/v4-mini/index.ts", "types": "./v4-mini/index.d.cts", "import": "./v4-mini/index.js", "require": "./v4-mini/index.cjs"}, "./v4/mini": {"@zod/source": "./src/v4/mini/index.ts", "types": "./v4/mini/index.d.cts", "import": "./v4/mini/index.js", "require": "./v4/mini/index.cjs"}, "./v4/core": {"@zod/source": "./src/v4/core/index.ts", "types": "./v4/core/index.d.cts", "import": "./v4/core/index.js", "require": "./v4/core/index.cjs"}, "./v4/locales": {"@zod/source": "./src/v4/locales/index.ts", "types": "./v4/locales/index.d.cts", "import": "./v4/locales/index.js", "require": "./v4/locales/index.cjs"}, "./v4/locales/*": {"@zod/source": "./src/v4/locales/*", "types": "./v4/locales/*", "import": "./v4/locales/*", "require": "./v4/locales/*"}}}
// @filename: node_modules/zod/v4/classic/package.json
{"type": "module", "main": "./index.cjs", "module": "./index.js", "types": "./index.d.cts"}
// @filename: node_modules/zod/v4/core/package.json
{"type": "module", "main": "./index.cjs", "module": "./index.js", "types": "./index.d.cts"}
// @filename: node_modules/zod/v4/package.json
{"type": "module", "main": "./index.cjs", "module": "./index.js", "types": "./index.d.cts"}
// @filename: package.json
{"type": "module"}
// @filename: modules/sqon/src/builder/index.test.ts
import { SqonBuilder } from '#builder/index.js';
		const study = { op: 'in', content: { fieldName: 'study', value: ['a'] } } as const;
		const site = { op: 'in', content: { fieldName: 'site', value: ['b'] } } as const;
			assert.deepEqual(SqonBuilder.or([study, site]).toValue(), { op: 'or', content: [study, site] });
// @filename: modules/sqon/src/builder/index.ts
import type { SqonCombination, SqonNode } from '#schema/index.js';
/**
 */
export const SqonBuilder = {
	or: (content: SqonNode | SqonNode[], pivot?: string): SqonBuilderHandle =>
		createBuilder(emptySqon()).or(content, pivot),
};
// @filename: modules/sqon/src/schema/constants.ts
// @filename: modules/sqon/src/schema/index.ts
import { z as zod } from 'zod';
const FieldNameSchema = zod
export const InLikeFilterSchema = zod.looseObject({
		fieldName: FieldNameSchema,
});
export const AllFilterSchema = zod.looseObject({
	op: zod.literal('all'),
});
export const RangeLikeFilterSchema = zod.looseObject({
		fieldName: FieldNameSchema,
});
export const BetweenFilterSchema = zod.looseObject({
	op: zod.literal('between'),
});
export const WildcardFilterSchema = zod.looseObject({
	op: zod.union([zod.literal('wildcard'), zod.literal('filter')]),
});
export type { SqonCombination, SqonLeaf, SqonNode } from './types.js';
// @filename: modules/sqon/src/schema/types.ts
import type { z as zod } from 'zod';
import type {
	AllFilterSchema,
} from './index.js';
export type SqonLeaf =
	| zod.infer<typeof AllFilterSchema>
export type SqonCombination = {
	content: SqonNode[];
};
export type SqonNode = SqonLeaf | SqonCombination;
// @filename: node_modules/@types/node/index.d.ts
export {};
// @filename: node_modules/zod/index.d.cts
import * as z from "./v4/classic/external.cjs";
export { z, z as default };
// @filename: node_modules/zod/v4/classic/external.d.cts
export * from "./schemas.cjs";
export type { infer } from "../core/index.cjs";
// @filename: node_modules/zod/v4/classic/schemas.d.cts
import * as core from "../core/index.cjs";
import { util, type $ZodNumberFormats } from "../core/index.cjs";
export interface ZodType<out Output = unknown, out Input = unknown, out Internals extends core.$ZodTypeInternals<Output, Input> = core.$ZodTypeInternals<Output, Input>> extends core.$ZodType<Output, Input, Internals> {
}
export interface _ZodType<out Internals extends core.$ZodTypeInternals = core.$ZodTypeInternals> extends ZodType<any, any, Internals> {
}
export interface ZodObject<
out Shape extends core.$ZodShape = core.$ZodLooseShape, out Config extends core.$ZodObjectConfig = core.$strip> extends _ZodType<core.$ZodObjectInternals<Shape, Config>>, core.$ZodObject<Shape, Config> {
}
export declare function looseObject<T extends core.$ZodLooseShape>(shape: T, params?: string | core.$ZodObjectParams): ZodObject<util.Writeable<T>, core.$loose>;
export interface ZodLiteral<T extends util.Literal = util.Literal> extends _ZodType<core.$ZodLiteralInternals<T>>, core.$ZodLiteral<T> {
}
export declare function literal<const T extends util.Literal>(value: T, params?: string | core.$ZodLiteralParams): ZodLiteral<T>;
// @filename: node_modules/zod/v4/core/core.d.cts
export type output<T> = T extends {
} ? T["_zod"]["output"] : unknown;
export type { output as infer };
// @filename: node_modules/zod/v4/core/index.d.cts
export * from "./core.cjs";
export * from "./schemas.cjs";
export * as util from "./util.cjs";
// @filename: node_modules/zod/v4/core/schemas.d.cts
import * as util from "./util.cjs";
export interface $ZodTypeInternals<out O = unknown, out I = unknown> extends _$ZodTypeInternals {
    output: O;
}
export interface $ZodType<O = unknown, I = unknown, Internals extends $ZodTypeInternals<O, I> = $ZodTypeInternals<O, I>> {
    _zod: Internals;
}
/**
 */
export type $InferObjectOutput<T extends $ZodLooseShape, Extra extends Record<string, unknown>> = string extends keyof T ? util.IsAny<T[keyof T]> extends true ? Record<string, unknown> : Record<string, core.output<T[keyof T]>> : keyof (T & Extra) extends never ? Record<string, never> : util.Prettify<{
    -readonly [k in keyof T as T[k] extends OptionalOutSchema ? k : never]?: T[k]["_zod"]["output"];
} & Extra>;
export type $loose = {
    out: Record<string, unknown>;
};
export interface $ZodObjectInternals<
out Shape extends $ZodShape = $ZodShape, out Config extends $ZodObjectConfig = $ZodObjectConfig> extends _$ZodTypeInternals {
    output: $InferObjectOutput<Shape, Config["out"]>;
}
export interface $ZodLiteralInternals<T extends util.Literal = util.Literal> extends $ZodTypeInternals<T, T> {
}
// @filename: node_modules/zod/v4/core/util.d.cts
export type Writeable<T> = {
    -readonly [P in keyof T]: T[P];
} & {};
export type Prettify<T> = {
    [K in keyof T]: T[K];
} & {};
