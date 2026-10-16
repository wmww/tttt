// A reused `NLP['href'] | undefined` whose `NLP` is a local alias declaration emit can't name prints the indexed
// access's type in its place, a union: parenthesized as a union member (`(string | {...}) | undefined`, tsgo's
// factory parenthesizing a union constituent of a union) and as an array element (nextra's mdx-components `href?:`)
// @strict: true
// @declaration: true
// @filename: /c.ts
type Url = { u: 1 } | string;
export interface LinkProps<T = any> { href: Url; as?: Url; t?: T }
// @filename: /a.ts
import type { LinkProps } from './c';
type NLP = LinkProps;
type Props = Omit<NLP, 'href'> & { href?: NLP['href'] | undefined; list?: NLP['href'][] };
type FC<P> = (p: P) => void;
export const Anchor: FC<Props> = () => {};
// @filename: /b.ts
import { Anchor } from './a';
import type { LinkProps } from './c';
export type Again = LinkProps;
export const comps = { a: Anchor };
