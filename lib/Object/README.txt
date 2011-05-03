Welcome to the Object library!

The purpose of this document is to explain where this library currently stands,
and where it is going. Any questions should be directed at the llvm-dev list,
but also cc <bigcheesegs@gmail.com>.

Purpose
=======

The purpose of this library is to provide a common API for reading, manipulating
, and creating object files of various formats. It is very similar in purpose to
the existing BFD (Binary File Descriptor) library. The end goal is to use this
library to replace all of the binutils, including the linker.

Status
======

Not much has been done, just enough for reading symbols and sections. nm works,
and a small part of objdump works. ELF and COFF, no MachO. Read only, no write.
No error handling. Limited information.

TODO
====

* Add error_code based error handling.
* Add relocation reading.
* Design how to get access to details.
* Design how to differentiate archives of object files from just object files.
* Design how to modify object files without exploading memory useage.

Current Thoughts on Design
==========================

Archives
********

Archives are different from normal object files in that they contain child
object files, but still have a symbol table.
