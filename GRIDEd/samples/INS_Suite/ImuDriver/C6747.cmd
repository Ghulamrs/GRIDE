/* c6747.cmd - linker command file for TMS320C6747 (adjust to your board) */
-stack 0x2000
-heap  0x2000

MEMORY
{
    L2RAM:   o = 0x11800000  l = 0x00040000   /* 256 KB internal L2 */
    SHRAM:   o = 0x80000000  l = 0x00020000   /* 128 KB shared RAM  */
    SDRAM:   o = 0xC0000000  l = 0x04000000   /* external SDRAM     */
}

SECTIONS
{
    .vectors  > L2RAM
    .text     > L2RAM
    .stack    > L2RAM
    .bss      > L2RAM
    .cinit    > L2RAM
    .const    > L2RAM
    .data     > L2RAM
    .switch   > L2RAM
    .sysmem   > SDRAM
    .far      > SDRAM
    .cio      > L2RAM
}
