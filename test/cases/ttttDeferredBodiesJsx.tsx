// @jsx: react-jsx
// @strict: true
// Plan 78 stage 3: .tsx function bodies are deferred too, the skipper stepping over their JSX (skip_jsx_element(): text
// with quotes and slashes, braces in attributes and children, fragments, comments in children; a generic arrow
// function's `<T,>` gives up, `<` after a name is an operator). a.tsx's only JSX is in a body: under react-jsx it still
// makes the file a module (moduleDetection), so its `x` doesn't clash with script b.tsx's. Run with baseline_tests
// --defer-bodies
// @filename: a.tsx
function view(items: string[], n: number) {
    const less = n < 2 && items.length >1;
    return <div className="it's {fine}" data-n={n > 1 ? "a" : 'b'} {...{ title: id("t") }}>
        don't "quote" / slash {items.map(i => <span key={i}>{i}</span>)}
        <>{/* a comment */}{less ? <br/> : null}</>
    </div>;
}
function id(t: string) {
    const f = <T,>(x: T) => x;
    return f(t);
}
const x = view([], 1);
// @filename: b.tsx
const x = "b";
