// @lib: dom,webworker
// @skipDefaultLibCheck: false
// Two lib files that conflict (tsgo checks them unless skipLibCheck/skipDefaultLibCheck): a duplicate between them is
// reported at both declarations, and a property declared again with another type by a later lib's interface is TS2717
const sentinel: number = "";
