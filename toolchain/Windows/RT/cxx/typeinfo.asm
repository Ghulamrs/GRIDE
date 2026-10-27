; typeinfo.asm - ??_7type_info@@6B@, the vftable every type descriptor points at.
;
; Spec: the Microsoft C++ ABI's RTTI - each type descriptor (??_R0...) a cpp11 program emits begins with
; the address of type_info's vftable, as cl's do and as vcruntime defines it: one slot, the scalar
; deleting destructor, and before it the place a complete object locator would be (none here, as
; type_info is never itself the operand of typeid). The descriptors are static data, never deleted,
; so the destructor only answers the object.

PUBLIC ??_7type_info@@6B@
PUBLIC ??_Gtype_info@@UEAAPEAXI@Z

CONST SEGMENT READONLY ALIGN(16) 'DATA'
  DQ 0
??_7type_info@@6B@ DQ ??_Gtype_info@@UEAAPEAXI@Z
CONST ENDS

.CODE
??_Gtype_info@@UEAAPEAXI@Z PROC
  mov rax, rcx
  ret
??_Gtype_info@@UEAAPEAXI@Z ENDP

END
