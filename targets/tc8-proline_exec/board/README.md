TC8 DDR config is byte-for-byte identical to C60's, minus the 5 trailing
ANATOP/CCM register writes the C60 fork appended (see
`re/c60_ddr_extraction.md`). Symlinking to the C60 copy for now; if TC8
needs anything board-specific later, fork as needed.
