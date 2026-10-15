// @declaration: true
// An empty tuple written after a line break is a multi-line list: the printer writes its line break
type M<a, b, c, d> = [a, b, c, d];
export namespace N {
	export type multiply<t, count extends number> = M<
		t,
		[],
		count,
		[]
	>
}
export type E = M<1, [], 2,
	[]>;
export type F = [];
