/****************************************************************************/
/*  C6747.cmd                                                               */
/*  Copyright (c) 2010  Texas Instruments Incorporated                      */
/*  Author: Rafael de Souza                                                 */
/*                                                                          */
/*    Description: This file is a sample linker command file that can be    */
/*                 used for linking programs built with the C compiler and  */
/*                 running the resulting .out file on an C6747              */
/*                 device.  Use it as a guideline.  You will want to        */
/*                 change the memory layout to match your specific          */
/*                 target system.  You may want to change the allocation    */
/*                 scheme according to the size of your program.            */
/*                                                                          */
/****************************************************************************/

MEMORY
{
    DSPL2ROM     o = 0x00700000  l = 0x00100000
    SHRAM        o = 0x00800000  l = 0x00040000
}                                                                       

SECTIONS
{
    .text          >  SHRAM
    .stack         >  SHRAM
    .bss           >  SHRAM
    .cio           >  SHRAM
    .const         >  SHRAM
    .data          >  SHRAM
    .sysmem        >  SHRAM
    .far           >  SHRAM
  
    /* COFF sections */
    .pinit         >  SHRAM
    .cinit         >  SHRAM
  
    /* EABI sections */
    .binit         >  SHRAM
    .init_array    >  SHRAM
    .neardata      >  SHRAM
    .fardata       >  SHRAM
    .rodata        >  SHRAM
    .c6xabi.exidx  >  SHRAM
    .c6xabi.extab  >  SHRAM
}
