// @strict: true
interface RefAttributes<T> { ref?: T }
interface SVGAttributes<T> { className?: string }
interface SVGProps<T> extends SVGAttributes<T>, RefAttributes<T> {}
type ElementAttributes = RefAttributes<number> & Partial<SVGProps<number>>;
interface LucideProps extends ElementAttributes {
    size?: string | number;
}
const x: Omit<LucideProps, 'ref'> = { className: "size-5" };
type K = keyof LucideProps;
const k: K = "className";
