// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "target": "es2022", "module": "esnext", "moduleResolution": "bundler", "types": []}, "files": ["before.ts", "zafter.ts"]}
// @filename: before.ts
css2({ ':hover': { notAProperty: '1' } });
export declare function css2(style: Style2): void;
type Pseudos2 = ':hover';
export type CSSPseudos2 = { [P in Pseudos2]?: Omit<Style2, Pseudos2> };
export type Style2 = CSSPseudos2 & { top?: string };
// @filename: zafter.ts
import { css2 } from './before';
css2({ ':hover': { notAProperty: '2' } });
