// @strict: true
// @module: nodenext
// A homomorphic mapped alias written through an import (`RO<Lang>`, its default left out) instantiated over a non-union
// goes by the declaration's alias with every argument (tsc's instantiateMappedType makes the constituent with no alias)
// @filename: lib.ts
export interface TL { names: readonly string[]; getOptions(): object }
export declare const all: readonly [{ names: readonly ["a", "aa"]; getOptions(): { x: { v: 1 } } }, { names: readonly ["b"]; getOptions(): { y: { v: 2 } } }];
export type AllLanguages = (typeof all)[number];
export type LanguageName<Language extends TL = AllLanguages> = Language["names"][number];
export type NameMap = { [Language in AllLanguages as LanguageName<Language>]: Language };
export type OptionMap<T> = { [K in keyof T]: T[K] extends { v: infer V } ? V : never };
export type RawMap = Readonly<{ [Lang in keyof NameMap]: ReturnType<NameMap[Lang]["getOptions"]> }>;
export type OptMap = Readonly<{ [Lang in keyof RawMap]: OptionMap<RawMap[Lang]> }>;
export type RO<L extends LanguageName = LanguageName, O = OptMap[L]> = { -readonly [K in keyof O]: O[K] };
// @filename: run.ts
import type { RO, LanguageName } from "./lib.js";
export interface NI<Lang extends LanguageName> { lang: Lang; ro: Partial<RO<Lang>> }
export type Options<Lang extends LanguageName = LanguageName> = NI<Lang> & { f: boolean };
export declare function q<Lang extends LanguageName = LanguageName>(o: Partial<Options<Lang>>): void;
// @filename: t.ts
import type { RO } from "./lib.js";
import { q } from "./run.js";
declare const bad: RO;
q({ lang: "a", ro: bad });
