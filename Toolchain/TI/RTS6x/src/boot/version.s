; Spec: none - RTS6x's own name and version, so that an image says which run-time it carries.
	.global	__rts6x_version
	.sect	".const"
__rts6x_version:
	.string	"RTS6x 1.2, a TMS320C6747 run-time support library"
	.byte	0
