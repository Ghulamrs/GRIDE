; Spec: SPRAB89B 18 and lnk6x's .sysmem rule: an allocator brings a .sysmem input section and the
; linker makes the section --heap_size bytes, __TI_SYSMEM_SIZE its size. __rts6x_heap is its start.
	.global	__rts6x_heap
__rts6x_heap	.usect	".sysmem", 8, 8
