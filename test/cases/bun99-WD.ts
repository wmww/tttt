// @filename: tsconfig.json
{"compilerOptions":{"strict":true,"noEmit":true,"target":"es2022","module":"esnext","moduleResolution":"bundler","types":[],"skipLibCheck":true},"files":["index.ts"]}
// @filename: index.ts
import { css, Style } from './lib';
css({ ':hover': { notAProperty: '1' } });
const p: Pick<Style, ':hover'> = {};
const q: Pick<Style, ':hover' | 'top'> = {};
const o: Omit<Style, 'top'> = {};
const r: Readonly<Style> = {};
const all: Required<Pick<Style, ':hover'>> = {};
declare const picked: Pick<Style, 'top'>;
picked.top = '';
const hover: never = null! as Style[':hover'];
const keys: never = null! as keyof Pick<Style, ':hover' | 'top'>;
export const sentinel: number = '';
// @filename: lib.d.ts
type Pseudos = ':hover';
export type CSSPseudos = { [P in Pseudos]?: Omit<Style, Pseudos> };
export type Style = CSSPseudos & { readonly top?: string };
export declare function css(style: Style): void;
