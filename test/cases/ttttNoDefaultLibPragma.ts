// `/// <reference no-default-lib="true"/>` changes nothing in tsgo 7 (processPragmasIntoFields ignores it): the file is
// no default library, so skipDefaultLibCheck (on in the harness) still checks it, and the libs are loaded
// @skipDefaultLibCheck: true
/// <reference no-default-lib="true"/>
const x: number = "s";
const a: Array<string> = [];
