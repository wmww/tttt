// @jsx: preserve
// @allowJs: true
// @checkJs: true
// @noEmit: true
// A JSX child element that ends without its `>` at a `<<`, `<<=` or `<=` leaves tsgo's parser token the ordinary one:
// the next child's `<` is both its opening and its type arguments' (parseJsxChildren's ReScanJsxToken)
// @filename: a.tsx
var a = (<a><c<T><<U>()=>void>/></a>);
var b = (<a><c x="1"/<<T>() => 1>/></a>);
var c = (<a><b></b<<T>()=>void>/></a>);
var d = (<><c<<T>()=>void>/></>);
var e = (<a><c<<<T>()=>void>/></a>);
var g = (<a><c<=>/></a>);
// @filename: b.jsx
var v = (<a><c<<T>()=>void>/></a>);
var w = (<a><c<=x/></a>);
