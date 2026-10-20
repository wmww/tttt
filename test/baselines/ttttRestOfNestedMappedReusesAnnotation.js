//// [test/cases/ttttRestOfNestedMappedReusesAnnotation.ts] ////

//// [rn.d.ts]
export interface Info { name: string; label?: string }
export interface P {
    a?: ReadonlyArray<Info> | undefined;
    live?: 'none' | 'polite' | undefined;
    onLayout?: () => void;
}
//// [main.ts]
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


//// [main.js]
const { onLayout: l1, ...rest1 } = pk;
export const v1 = rest1;
const { onLayout: l2, ...rest2 } = rp;
export const v2 = rest2;
const { onLayout: l3, ...rest3 } = pr;
export const v3 = rest3;
const { onLayout: l4, ...rest4 } = ro;
export const v4 = rest4;


//// [main.d.ts]
export declare const v1: {
    a?: ReadonlyArray<import("./rn").Info> | undefined;
    live?: 'none' | 'polite' | undefined;
};
export declare const v2: {
    a?: ReadonlyArray<import("./rn").Info> | undefined;
    live?: 'none' | 'polite' | undefined;
};
export declare const v3: {
    a?: ReadonlyArray<import("./rn").Info> | undefined;
    live?: 'none' | 'polite' | undefined;
};
export declare const v4: {
    a?: ReadonlyArray<import("./rn").Info> | undefined;
    live?: 'none' | 'polite' | undefined;
    children: string;
};
