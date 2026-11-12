; Spec: none - RTS6x's own name and version, so that an image says which run-time it carries, and printf6x's,
; which is made of RTS6x's own objects (printf6x.members) and so is numbered with it: the two numbers move together.
; Each string has the shape the toolchain's programs answer --version in - `name x.y - what it is` - and an IDE's
; About reads it out of the library (2026-11-12); this file is a member of both libraries.
	.global	__rts6x_version
	.global	__printf6x_version
	.sect	".const"
__rts6x_version:
	.string	"RTS6x 1.2 - TMS320C6747 run-time support library"
	.byte	0
__printf6x_version:
	.string	"printf6x 1.2 - RTS6x's formatted output library"
	.byte	0
