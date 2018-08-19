
file(STRINGS ${gtmthreadgblasmaccess} asmaccesstypes REGEX "^[A-Za-z_]+")
foreach(asmaccess ${asmaccesstypes})
  string(REGEX REPLACE "^([A-Za-z_]+)[^A-Za-z_].*$" "ggo_\\1" asm "${asmaccess}")
  file(STRINGS ${gtmthreadgblasmhdr} asmdef REGEX ${asm})
  string(REGEX REPLACE "# +define +([A-Za-z_]+) +([0-9]+)" "\\1 = \\2" asmsign "${asmdef}")
  file(WRITE ${gtmthreadgblasmfile} "${asmsign}\n")
endforeach()
