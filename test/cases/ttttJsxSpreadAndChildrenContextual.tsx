// A JSX spread attribute's `&&` (not context sensitive) next to children: its arrow gets the attributes' contextual
// type, the children check (TS2710) reading the spread's type only after the attributes are checked (papra's Solid tag)
// @jsx: preserve
// @strict: true
declare namespace JSX {
    interface Element {}
    interface ElementChildrenAttribute { children: {} }
    interface IntrinsicElements { span: { onClick?: (e: MouseEvent) => void; children?: string } }
}
declare const x: boolean, y: string;
const a = <span {...(x && { onClick: (e) => {} })}>{y}</span>;
const b = <span {...(x ? { onClick: (e) => {} } : {})}>{y}</span>;
const c = <span {...{ onClick: (e) => {} }}>{y}</span>;
