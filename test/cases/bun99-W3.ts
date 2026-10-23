// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "target": "es2022", "module": "esnext", "moduleResolution": "bundler", "types": [], "skipLibCheck": true}, "files": ["index.ts", "before.ts", "after.ts"]}
// @filename: after.ts
type Pseudos3 = ':hover';
type CSSPseudos3 = { [P in Pseudos3]?: Omit<Style3, Pseudos3> };
type Style3 = CSSPseudos3 & { top?: string };
declare function css3(style: Style3): void;
css3({ ':hover': { notAProperty: '1' } });
export {};
// @filename: before.ts
css2({ ':hover': { notAProperty: '1' } });
declare function css2(style: Style2): void;
type Pseudos2 = ':hover';
type CSSPseudos2 = { [P in Pseudos2]?: Omit<Style2, Pseudos2> };
type Style2 = CSSPseudos2 & { top?: string };
export {};
// @filename: index.ts
import { css } from './lib';

css({ ':hover': { notAProperty: '1' } });
// @filename: lib.d.ts
type Pseudos = ':hover';
type CSSPseudos = { [P in Pseudos]?: Omit<Style, Pseudos> };
type Style = CSSPseudos & { top?: string };
export declare function css(style: Style): void;
export {};
