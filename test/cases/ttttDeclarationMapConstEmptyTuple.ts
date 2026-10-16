// @declaration: true
// @declarationMap: true
// An `as const` object with an empty array is its pseudo type too (the empty tuple is its tuple target, no
// reference): the property names are mapped like tsgo's (pi's tool prompt contributions)
export const c = {
	snippet: "List",
} as const;
export const e = {
	g: [],
} as const;
export const f = {
	g: [1],
} as const;
