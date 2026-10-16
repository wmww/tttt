//// [test/cases/ttttDeclarationIndexedFallbackConstituentParens.ts] ////

//// [c.ts]
type Url = { u: 1 } | string;
export interface LinkProps<T = any> { href: Url; as?: Url; t?: T }
//// [a.ts]
import type { LinkProps } from './c';
type NLP = LinkProps;
type Props = Omit<NLP, 'href'> & { href?: NLP['href'] | undefined; list?: NLP['href'][] };
type FC<P> = (p: P) => void;
export const Anchor: FC<Props> = () => {};
//// [b.ts]
import { Anchor } from './a';
import type { LinkProps } from './c';
export type Again = LinkProps;
export const comps = { a: Anchor };


//// [c.js]
export {};
//// [a.js]
export const Anchor = () => { };
//// [b.js]
import { Anchor } from './a';
export const comps = { a: Anchor };


//// [c.d.ts]
type Url = {
    u: 1;
} | string;
export interface LinkProps<T = any> {
    href: Url;
    as?: Url;
    t?: T;
}
export {};
//// [a.d.ts]
import type { LinkProps } from './c';
type NLP = LinkProps;
type Props = Omit<NLP, 'href'> & {
    href?: NLP['href'] | undefined;
    list?: NLP['href'][];
};
type FC<P> = (p: P) => void;
export declare const Anchor: FC<Props>;
export {};
//// [b.d.ts]
import type { LinkProps } from './c';
export type Again = LinkProps;
export declare const comps: {
    a: (p: Omit<LinkProps<any>, "href"> & {
        href?: (string | {
            u: 1;
        }) | undefined;
        list?: (string | {
            u: 1;
        })[];
    }) => void;
};
