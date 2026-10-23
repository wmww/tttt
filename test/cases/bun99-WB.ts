// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "target": "es2022", "module": "esnext", "moduleResolution": "bundler", "types": [], "skipLibCheck": true}, "files": ["index.ts", "before.ts"]}
// @filename: before.ts
css2({});
css2({ top: 'a' });
css2({ ':hover': {} });
declare function css2(style: Style2): void;
type Pseudos2 = ':hover';
type CSSPseudos2 = { [P in Pseudos2]: Omit<Style2, Pseudos2> };
type Style2 = CSSPseudos2 & { top?: string };
export {};
// @filename: index.ts
import { css, get } from './lib';
css({ ':hover': { notAProperty: '1' } });
css({});
css({ top: 'a' });
const s = get();
const a: { ':hover': { top?: string } } = s;
const b: { top?: string } = s;
export const sentinel: number = '';
// @filename: lib.d.ts
type Pseudos = ':hover';
type CSSPseudos = { [P in Pseudos]: Omit<Style, Pseudos> };
type Style = CSSPseudos & { top?: string };
export declare function css(style: Style): void;
export declare function get(): Style;
export {};
