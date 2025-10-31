/* Linker script for ESP32 plugin apps
 *
 * Plugins are loaded into IRAM at runtime and must be position-independent.
 * The VTable must be at a known location (start of binary).
 */

MEMORY
{
  /* Plugin code will be loaded into IRAM dynamically */
  plugin_mem (rwx) : ORIGIN = 0x0, LENGTH = 128K
}

SECTIONS
{
  /* VTable must be at offset 0 for easy discovery */
  .plugin_vtable 0x0 : {
    KEEP(*(.plugin_vtable))
  } > plugin_mem

  /* Code section */
  .text : {
    *(.text .text.*)
    *(.literal .literal.*)
  } > plugin_mem

  /* Read-only data */
  .rodata : ALIGN(4) {
    *(.rodata .rodata.*)
    *(.srodata .srodata.*)
  } > plugin_mem

  /* Initialized data */
  .data : ALIGN(4) {
    *(.data .data.*)
    *(.sdata .sdata.*)
  } > plugin_mem

  /* Uninitialized data */
  .bss : ALIGN(4) {
    *(.bss .bss.*)
    *(.sbss .sbss.*)
    *(COMMON)
  } > plugin_mem

  /* Discard unnecessary sections */
  /DISCARD/ : {
    *(.comment)
    *(.note.*)
    *(.eh_frame)
    *(.debug*)
  }
}
