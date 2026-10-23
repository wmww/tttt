// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "target": "es2022", "module": "esnext", "moduleResolution": "bundler", "types": [], "skipLibCheck": true}, "files": ["index.ts"]}
// @filename: index.ts
import { css, Style } from './lib';
css({ ':hover': { notAProperty: '1' } });
const p: Partial<Style> = { ':hover': {} };
declare const style: Style;
const spread = { ...style };
const hover: never = spread[':hover'];
const top: never = spread.top;
const required: Required<Style> = { top: '' };
const picked: never = null! as keyof Partial<Style>;
const wrong: Style = { tpo: '' };
export const sentinel: number = '';
// @filename: lib.d.ts
type Pseudos = ':hover';
export type CSSPseudos = { [P in Pseudos]?: Omit<Style, Pseudos> };
export type Style = CSSPseudos & { top?: string };
export declare function css(style: Style): void;
