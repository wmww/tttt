// Known failing ([design], missing_checks.md): tsgo resolves an unchecked with body's assignment target `z` when a
// later reference's flow walk (the second `with (obj)`'s `obj`) crosses it, TS2304; tttt never resolves it
declare var obj: any;
with (obj) { z = 10; }
with (obj) { y; }
