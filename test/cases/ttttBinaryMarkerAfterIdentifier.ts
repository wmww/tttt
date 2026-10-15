// A U+FFFD makes the rest of the file one token (NonTextFileMarkerTrivia), not the end of the file: no ASI before it
let v = 1;
v�;
let w = 2;
