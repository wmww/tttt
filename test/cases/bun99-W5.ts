// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "target": "es2022", "module": "esnext", "moduleResolution": "bundler", "types": []}, "files": ["index.ts", "zlib.ts"]}
// @filename: index.ts
import { css } from './zlib';
css({ ':hover': { notAProperty: '1' } });
// @filename: zlib.ts
type Pseudos = ':hover';
export type CSSPseudos = { [P in Pseudos]?: Omit<Style, Pseudos> };
export type Style = CSSPseudos & { top?: string };
export declare function css(style: Style): void;
