// origin: npm Doezer/Questarr @ de563f0 (tsconfig.bench.json), client/src/components/CompactGameCard.tsx:361, wouter 3.13 Link
// flip: yes
// @strict: true
// A generic component (Link<H = BH>) whose props are a union discriminated by an absent `asChild`: while the call infers H,
// the attribute function takes its contextual signature from the discriminated member
// @jsx: preserve
declare namespace JSX { interface Element {} interface IntrinsicElements {} }
interface Ev<T> { target: T; stop(): void }
type Handler<T = Element> = (e: Ev<T>) => void;
type Hook = (...args: any[]) => [string, (path: string, ...args: any[]) => any];
type Opts<H extends Hook> = NonNullable<Parameters<ReturnType<H>[1]>[1]>;
type Nav<H extends Hook> = { href: string } & Opts<H>;
type AsChild<C, D> = ({ asChild?: false } & D) | ({ asChild: true } & C);
type Props<H extends Hook> = Nav<H> & AsChild<{ onClick?: Handler }, { onClick?: Handler<HTMLAnchorElement> }>;
type BH = (o?: {}) => [string, (to: string, o?: { replace?: boolean }) => void];
declare function Link<H extends Hook = BH>(props: Props<H>): JSX.Element;
const a = <Link href="/x" onClick={(e) => { e.stop(); }} />;
const b = Link({ href: "/x", onClick: (e) => { e.stop(); } });
