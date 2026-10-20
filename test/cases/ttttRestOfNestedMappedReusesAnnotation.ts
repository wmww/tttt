// @declaration: true
// @strict: true

// The rest of a mapped type over a mapped type (`Readonly<Pick<P, K>>`, `Partial<Readonly<P>>`) reuses the annotations
// of the properties the inner one maps (tsc's mapped symbols carry their declarations all the way down):
// `ReadonlyArray<Info>` and `'none' | 'polite'` as written (react-three-fiber's react-native mock)
// @filename: rn.d.ts
export interface Info { name: string; label?: string }
export interface P {
    a?: ReadonlyArray<Info> | undefined;
    live?: 'none' | 'polite' | undefined;
    onLayout?: () => void;
}
// @filename: main.ts
import { P } from './rn';
declare const pk: Pick<P, 'a' | 'live' | 'onLayout'>;
const { onLayout: l1, ...rest1 } = pk;
export const v1 = rest1;
declare const rp: Readonly<Pick<P, 'a' | 'live' | 'onLayout'>>;
const { onLayout: l2, ...rest2 } = rp;
export const v2 = rest2;
declare const pr: Partial<Readonly<P>>;
const { onLayout: l3, ...rest3 } = pr;
export const v3 = rest3;
declare const ro: Readonly<Omit<P, 'x'>> & { children: string };
const { onLayout: l4, ...rest4 } = ro;
export const v4 = rest4;
