// Interface extending a type alias for an intersection loses the intersection's members
// (tttt: keyof X is just "x"; tsc: "a" | "b" | "x"). Reduced from lucide-react's
// `interface LucideProps extends ElementAttributes` where
// `type ElementAttributes = RefAttributes<SVGSVGElement> & Partial<SVGProps<SVGSVGElement>>`.
interface A { a?: string }
interface B { b?: string }
type I = A & B;
interface X extends I { x?: number }
const k1: keyof X = "a";
declare const x: X;
const s: string | undefined = x.b;
const o: Omit<X, "a"> = { b: "" };
