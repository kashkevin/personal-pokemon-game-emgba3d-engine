# Converts the GBA build's ARM GNU-as data syntax to x86 GNU-as syntax for the
# native PC build (Makefile_pc). Applied to fully preprocessed data streams
# (preproc has already inlined .include files and expanded .string text).
#
# 1. '@' starts a comment in ARM GNU as. Strip it when it is outside a string
#    and not escaped (\@ is the macro invocation counter).
s/^(([^"@\\]|\\.|"([^"\\]|\\.)*")*)@.*$/\1/
# 2. ARM .align takes a power of two; x86 ELF/PE .align takes a byte count.
#    .p2align has power-of-two semantics on every target, so data layout and
#    padding stay identical to the GBA build.
s/^([[:space:]]*)\.align([[:space:]]|$)/\1.p2align\2/
s/;([[:space:]]*)\.align([[:space:]]|$)/;\1.p2align\2/g
# 3. Sized data directives. On x86 .word is 2 bytes, so map it to .int (4 bytes).
s/\.4byte([[:space:]])/.int\1/g
s/\.2byte([[:space:]])/.short\1/g
s/\.word([[:space:]])/.int\1/g
# 4. PE/COFF (Windows) rejects ELF-only section types and .type/.size outside
#    .def blocks. Drop them; they only carry debug/symbol metadata.
s/^([[:space:]]*\.section[[:space:]]+[^,]+,[[:space:]]*"[^"]*")[[:space:]]*,[[:space:]]*[%@][a-z_]+.*$/\1/
/^[[:space:]]*\.type[[:space:]]/d
/^[[:space:]]*\.size[[:space:]]/d
