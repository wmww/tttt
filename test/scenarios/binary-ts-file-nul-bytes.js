currentDirectory::/home/src/workspaces/project
useCaseSensitiveFileNames::true
Input::
//// [/home/src/workspaces/project/tsconfig.json] *new* 
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [] } }
//// [/home/src/workspaces/project/a.ts] *new* 
const sentinel: number = "";
//// [/home/src/workspaces/project/chunk.ts] *base64*
R0AREABC8CUAAcEAAAAB/wAB/IAUSBIBBkZGbXBlZwlTZXJ2aWNlMDGneaAD////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////9HQAAQAACwDQABwQAAAAHwACqxBLL/////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////0dQABAAArAdAAHBAADhAPAAG+EA8AAP4QHwBgoEdW5kAAh96Hf/////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////R0EAMAdQAAB7DH4AAAAB4AAAgMAKMQAJEKERAAfYYQAAAAEJ8AAAAAFnZAAfrNlAUAW7ARAAAAMAEAAAAwMg8YMZ
YAAAAAFo6+PLIsAAAAEGBf//qdxF6b3m2Ui3lizYINkj7u94MjY0IC0gY29yZSAxNDYgcjEwOSAxMjEzOTZjIC0gSC4yNjQvTVBFRy00IEFWQyBjb2RlYyAt
IENvcHlsZWZ0IDIwMDMtMjAxNSAtIGh0dHA6Ly93d3dHAQARLnZpZGVvbGFuLm9yZy94MjY0Lmh0bWwgLSBvcHRpb25zOiBjYWJhYz0xIHJlZj0zIGRlYmxv
Y2s9MTowOjAgYW5hbHlzZT0weDM6MHgxMTMgbWU9aGV4IHN1Ym1lPTcgcHN5PTEgcHN5X3JkPTEuMDA6MC4wMCBtaXhlZF9yZWY9MSBtZV9yYW5nZT0xNiBj
aHJvbWFfbWU9MSB0cmVsbGlzPTEgOHg4ZGN0PTEgY3FtPTAgZGVhZEcBABJ6b25lPTIxLDExIGZhc3RfcHNraXA9MSBjaHJvbWFfcXBfb2Zmc2V0PS0yIHRo
cmVhZHM9NDggbG9va2FoZWFkX3RocmVhZHM9NSBzbGljZWRfdGhyZWFkcz0wIG5yPTAgZGVjaW1hdGU9MSBpbnRlcmxhY2VkPTAgYmx1cmF5X2NvbXBhdD0w
IGNvbnN0cmFpbmVkX2ludHJhPTAgYmZyYW1lcz0zIGJfcHlyYW1pZD0yIGJfYWRhRwEAE3B0PTEgYl9iaWFzPTAgZGlyZWN0PTEgd2VpZ2h0Yj0xIG9wZW5f
Z29wPTAgd2VpZ2h0cD0yIGtleWludD0yNSBrZXlp

tsgo -p . --noEmit
ExitStatus:: DiagnosticsPresent_OutputsGenerated
Output::
chunk.ts(1,1): error TS1490: File appears to be binary.
chunk.ts(1,1): error TS1434: Unexpected keyword or identifier.
chunk.ts(1,3): error TS1127: Invalid character.
chunk.ts(1,4): error TS1127: Invalid character.
chunk.ts(1,5): error TS1127: Invalid character.
chunk.ts(1,6): error TS1434: Unexpected keyword or identifier.
chunk.ts(1,7): error TS1128: Declaration or statement expected.

