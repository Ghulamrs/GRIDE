; Spec: Itanium C++ ABI 2.9.4-2.9.5 - the vtables of __cxxabiv1's type_info classes; a
; type_info object's first word is its class's vtable plus 8, the address point. Nothing calls
; through them (RTS6x tells the kinds apart by that address), so the two slots are zero.
; Written by tools/gen-typeinfo.py.

	.global	_ZTVN10__cxxabiv123__fundamental_type_infoE
	.global	__rts6x_vt_fundamental
	.global	_ZTVN10__cxxabiv117__array_type_infoE
	.global	__rts6x_vt_array
	.global	_ZTVN10__cxxabiv120__function_type_infoE
	.global	__rts6x_vt_function
	.global	_ZTVN10__cxxabiv116__enum_type_infoE
	.global	__rts6x_vt_enum
	.global	_ZTVN10__cxxabiv117__class_type_infoE
	.global	__rts6x_vt_class
	.global	_ZTVN10__cxxabiv120__si_class_type_infoE
	.global	__rts6x_vt_si_class
	.global	_ZTVN10__cxxabiv121__vmi_class_type_infoE
	.global	__rts6x_vt_vmi_class
	.global	_ZTVN10__cxxabiv117__pbase_type_infoE
	.global	__rts6x_vt_pbase
	.global	_ZTVN10__cxxabiv119__pointer_type_infoE
	.global	__rts6x_vt_pointer
	.global	_ZTVN10__cxxabiv129__pointer_to_member_type_infoE
	.global	__rts6x_vt_pointer_to_member
	.sect	".const"
	.align	4
_ZTVN10__cxxabiv123__fundamental_type_infoE:
	.word	0
	.word	0
__rts6x_vt_fundamental:
	.word	0
	.word	0
_ZTVN10__cxxabiv117__array_type_infoE:
	.word	0
	.word	0
__rts6x_vt_array:
	.word	0
	.word	0
_ZTVN10__cxxabiv120__function_type_infoE:
	.word	0
	.word	0
__rts6x_vt_function:
	.word	0
	.word	0
_ZTVN10__cxxabiv116__enum_type_infoE:
	.word	0
	.word	0
__rts6x_vt_enum:
	.word	0
	.word	0
_ZTVN10__cxxabiv117__class_type_infoE:
	.word	0
	.word	0
__rts6x_vt_class:
	.word	0
	.word	0
_ZTVN10__cxxabiv120__si_class_type_infoE:
	.word	0
	.word	0
__rts6x_vt_si_class:
	.word	0
	.word	0
_ZTVN10__cxxabiv121__vmi_class_type_infoE:
	.word	0
	.word	0
__rts6x_vt_vmi_class:
	.word	0
	.word	0
_ZTVN10__cxxabiv117__pbase_type_infoE:
	.word	0
	.word	0
__rts6x_vt_pbase:
	.word	0
	.word	0
_ZTVN10__cxxabiv119__pointer_type_infoE:
	.word	0
	.word	0
__rts6x_vt_pointer:
	.word	0
	.word	0
_ZTVN10__cxxabiv129__pointer_to_member_type_infoE:
	.word	0
	.word	0
__rts6x_vt_pointer_to_member:
	.word	0
	.word	0
