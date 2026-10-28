// TS1358 for every tagged template whose tag is part of an optional chain (tsc's parseTaggedTemplateRest flags it
// OptionalChain), until parentheses or a `!` end the chain
declare const o: any;
o?.a`x`;
o?.a`x``y`;
o?.a`x`.b`y`;
o?.a`x${1}``y${2}`;
o?.a`x`()`y`;
o?.a`x`[0]`y`;
o?.a`x`!`y`;
(o?.a`x`)`y`;
o?.a`x`.b.c;
o?.a<string>`x``y`;
