#################################################################
#								#
# Copyright (c) 2012-2018 Fidelity National Information		#
# Services, Inc. and/or its subsidiaries. All rights reserved.	#
#								#
# Copyright (c) 2017-2018 YottaDB LLC. and/or its subsidiaries.	#
# All rights reserved.						#
#								#
# Copyright (c) 2017 Stephen L Johnson. All rights reserved.	#
#								#
#	This source code contains the intellectual property	#
#	of its copyright holder(s), and is made available	#
#	under a license.  If you do not know the terms of	#
#	the license, please stop and do not read further.	#
#								#
#################################################################

# CMake 2.8.4 introduced LINK_DEPENDS target property
# CMake 2.8.4 introduced generator expressions in custom commands
# CMake 2.8.5 introduced use of C compiler as the assembler
# CMake 2.8.12 Adds MacOSX RPATH
cmake_minimum_required(VERSION 2.8.12)
project(YDB C ASM)

# Max optimization level is -O2
get_property(languages GLOBAL PROPERTY ENABLED_LANGUAGES)
foreach(lang ${languages})
  get_property(flags CACHE CMAKE_${lang}_FLAGS_RELEASE PROPERTY VALUE)
  if("${flags}" MATCHES "-O[3-9]")
    string(REGEX REPLACE "-O[3-9]" "-O2" flags "${flags}")
    set_property(CACHE CMAKE_${lang}_FLAGS_RELEASE PROPERTY VALUE "${flags}")
  endif()
endforeach()

# Defaults
set(version r123)

# If CMAKE_BUILD_TYPE is not defined make it a Release build
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# If CMAKE_BUILD_TYPE is set to Debug, then CMAKE_C_FLAGS_DEBUG variable contents will be added at the end of
# CMAKE_C_FLAGS during compilation (likewise with CMAKE_ASM_FLAGS_DEBUG). Keep these debug variables defined and ready.
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -DDEBUG")
if("${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
  set(CMAKE_ASM_FLAGS_DEBUG "${CMAKE_ASM_FLAGS_DEBUG}")
elseif("${CMAKE_C_COMPILER_ID}" MATCHES "GNU")
  set(CMAKE_ASM_FLAGS_DEBUG "${CMAKE_ASM_FLAGS_DEBUG} -Wa,--defsym,DEBUG=1")
endif()

set(install_permissions_script
  OWNER_READ OWNER_EXECUTE OWNER_WRITE
  GROUP_READ GROUP_EXECUTE
  WORLD_READ WORLD_EXECUTE
  )

set_property(DIRECTORY APPEND PROPERTY $<$<CONFIG:Debug>:DEBUG)

# Only IA64 and x86_64 architectures use this option
set(gen_xfer_desc 0)

# Platform specific libs
set(gtm_osarch_libs "")

# Define these ahead of establishing platforms
set(sources_used "")
set(extralibs "")
message("--> OS = ${CMAKE_SYSTEM_NAME} / ARCH = ${CMAKE_SYSTEM_PROCESSOR}")
# Establish platform
# Except for Solaris, CMAKE_COMPILER_IS_GNUCC is true
if("${CMAKE_SYSTEM_NAME}" MATCHES "Linux")
  include(sr_linux/platform.cmake)
elseif("${CMAKE_SYSTEM_NAME}" MATCHES "HP-UX")
  include(sr_hpux/platform.cmake)
elseif("${CMAKE_SYSTEM_NAME}" MATCHES "AIX")
  include(sr_aix/platform.cmake)
elseif("${CMAKE_SYSTEM_NAME}" MATCHES "SunOS")
  include(sr_sun/platform.cmake)
elseif("${CMAKE_SYSTEM_NAME}" MATCHES "Darwin")
  include(sr_darwin/platform.cmake)
else()
  message(FATAL_ERROR "--> OS = ${CMAKE_SYSTEM_NAME} / ARCH = ${CMAKE_SYSTEM_PROCESSOR}")
endif()

# Choose where to get bootstrap sources.
set(YDB_DIST "" CACHE PATH "Existing YottaDB Distribution")
if(YDB_DIST)
  # Bootstrap using already-installed mumps.
  message(STATUS "Using existing ydb_dist=${YDB_DIST} to generate sources.")
  set(gen_bootstrap 1)
  set(gen_bootstrap_dist "${YDB_DIST}")
  set(gen_bootstrap_mumps "${YDB_DIST}/mumps")
  set(gen_bootstrap_depend "${gen_bootstrap_mumps}")
else()
  # Bootstrap using pre-generated bootstrap sources.
  # We generate them again only for comparison.
  message(STATUS "Bootstrapping from pre-generated sources.")
  set(gen_bootstrap 0)
  set(gen_bootstrap_dist "${YDB_BINARY_DIR}")
  set(gen_bootstrap_mumps "$<TARGET_FILE:mumps>")
  set(gen_bootstrap_depend mumps)
endif()

set(YDB_INSTALL_DIR yottadb_${version} CACHE STRING "Install directory")
if(NOT YDB_INSTALL_DIR)
  set(YDB_INSTALL_DIR .)
endif()

#-----------------------------------------------------------------------------

# Source files are organized into subdirectories named by platform.
# Store in 'gt_src_list' a list of directory names selected and
# ordered based on the target platform.  A given source file name may
# appear in multiple directories but only the instance found earliest
# in the search path will be used.  Later instances of the same source
# file name will be ignored.
list(APPEND gt_src_list
  sr_unix_gnp
  sr_unix_cm
  sr_unix
  sr_port_cm
  sr_port
  )

# Collect the source file names belonging to each directory.  Store
# the full path to a directory in 'source_dir_${d}' and the list of
# source file names in the directory in 'sources_${d}'.
foreach(d ${gt_src_list})
  set(source_dir_${d} ${YDB_SOURCE_DIR}/${d})
  file(GLOB sources_${d} RELATIVE ${source_dir_${d}}
    ${d}/*.c ${d}/*.s ${d}/*.si ${d}/*.txt)
endforeach()
# Prefer generated sources over all other locations.
file(MAKE_DIRECTORY ${YDB_BINARY_DIR}/genused)
file(MAKE_DIRECTORY ${YDB_BINARY_DIR}/gennotused)
set(source_dir_genused ${YDB_BINARY_DIR}/genused)
# Hard-code list since we cannot glob files that do not exist.
set(sources_gen gtm_threadgbl_deftypes.h gtm_threadgbl_deftypes_asm.si)
if(gen_bootstrap)
  set(sources_gen ${sources_gen} cmerrors_ctl.c cmierrors_ctl.c gdeerrors_ctl.c merrors_ctl.c ttt.c)
endif()

# The C preprocessor include search path consists of every source
# directory ordered by 'gt_src_list'.
set(includes ${YDB_BINARY_DIR}/genused)
foreach(d ${gt_src_list})
  list(APPEND includes ${source_dir_${d}})
endforeach()
include_directories(${includes})
# We generate some uniquely-named headers in the top of the build tree.
include_directories(${YDB_BINARY_DIR})
include_directories(/usr/include/openssl-1.0)

# Function to select the first instance of a given source file name
# found in the ordered 'gt_src_list' search path.
function(select_file src_var name)
  set(found "")
  foreach(d ${gt_src_list})
    set(src "${source_dir_${d}}/${name}")
    if(EXISTS "${src}")
      set(found "${src}")
      break()
    endif()
  endforeach()
  if(NOT found)
    message(FATAL_ERROR "Cannot find \"${name}\" as ${ARGN}")
  endif()
  set("${src_var}" "${found}" PARENT_SCOPE)
endfunction()

# Macro to search directories ordered by 'gt_src_list' for a given
# list of source file names, assign them to a target, and exclude the
# source file names from inclusion in libmumps.
macro(set_source_list target)
  foreach(name ${ARGN})
    set(src "")
    foreach(d ${gt_src_list})
      if(";${sources_${d}};" MATCHES ";(${name}\\.(c|s|si));")
        set(fname ${CMAKE_MATCH_1})
        set(src ${d}/${fname})
        get_filename_component(fnoext ${fname} NAME_WE)
        set("source_used_${fnoext}" 1)
        list(APPEND sources_used ${source_dir_${d}}/${fname})
        break()
      endif()
    endforeach()
    if(src)
      list(APPEND ${target}_SOURCES ${src})
    else()
      message(FATAL_ERROR "No source \"${name}\" available!")
    endif()
  endforeach()
endmacro()

# Macro like set_source_list but that loads the list of source file
# names from a file.
macro(load_source_list target listfile)
  file(STRINGS "${listfile}" names)
  set_source_list(${target} ${names})
endmacro()

#-----------------------------------------------------------------------------

# Assign sources to executables.
set_source_list(gtm_threadgbl_deftypes gtm_threadgbl_deftypes)
set_source_list(dbcertify        dbcertify dlopen_libyottadb)
set_source_list(dse              dse dlopen_libyottadb)
set_source_list(ftok             ftok dlopen_libyottadb)
set_source_list(gtcm_gnp_server  gtcm_gnp_server dlopen_libyottadb)
set_source_list(gtcm_pkdisp      gtcm_pkdisp omi_extstr)
set_source_list(gtcm_play        gtcm_play dlopen_libyottadb)
set_source_list(gtcm_server      gtcm_server dlopen_libyottadb)
set_source_list(gtcm_shmclean    gtcm_shmclean dlopen_libyottadb)
set_source_list(gtmsecshr        gtmsecshr_wrapper)
if("${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
        set_source_list(gtmsecshr_real   gtm_icu)
endif()
set_source_list(gtmsecshr_real   gtmsecshr)
set_source_list(libgtmcrypt      gtmcrypt_ref gtmcrypt_pk_ref gtmcrypt_dbk_ref gtmcrypt_sym_ref gtmcrypt_util)
set_source_list(libgtmcrypt      ydb_getenv minimal_gbldefs)
if("${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
        set_source_list(libgtmtls   minimal_gbldefs ydb_getenv)
endif()
set_source_list(libgtmtls        gtm_tls_impl gtmcrypt_util)
if("${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
  set_source_list(libyottadb       gtm_icu tpdefs repl_errno)
endif()
set_source_list(libyottadb       gtm_main mupip_main mupip_cmd dse_main dse_cmd lke_main lke_cmd)
set_source_list(libyottadb       gtcm_play_main omi_srvc_xct omi_extstr)
set_source_list(libyottadb       gtcm_server_main gtcm_gnp_server_main gtcm_gnp_clitab gtcm_shmclean_main)
set_source_list(libyottadb       dbcertify_main dbcertify_cmd ftok_main)
set_source_list(lke              lke dlopen_libyottadb)
set_source_list(maskpass         maskpass gtmcrypt_util)
set_source_list(maskpass         ydb_getenv minimal_gbldefs)
set_source_list(mumps            gtm dlopen_libyottadb)
set_source_list(mupip            mupip dlopen_libyottadb)
set_source_list(semstat2         semstat2)

#-----------------------------------------------------------------------------
# libmumps gets leftover sources, so compute the remaining list.
set(source_used_dtgbldir 1) # exclude unused source
set_source_list(libmumps ydb_getenv minimal_gbldefs)
foreach(d ${gt_src_list})
  foreach(s ${sources_${d}})
    get_filename_component(snoext ${s} NAME_WE)
    if(NOT source_used_${snoext} AND "${s}" MATCHES "\\.(c|s|si)$")
      list(APPEND libmumps_SOURCES ${d}/${s})
      set(source_used_${snoext} 1)
      list(APPEND sources_used ${source_dir_${d}}/${s})
    endif()
  endforeach()
endforeach()

#-----------------------------------------------------------------------------
# Generate files depending on gtm

# Copy generation routines to a working directory.
foreach(m chk2lev.m chkop.m gendash.m genout.m loadop.m loadvx.m msg.m tttgen.m tttscan.m)
  add_custom_command(
    OUTPUT gennotused/${m}
    DEPENDS ${YDB_SOURCE_DIR}/sr_port/${m}
    COMMAND ${CMAKE_COMMAND} -E copy ${YDB_SOURCE_DIR}/sr_port/${m} ${YDB_BINARY_DIR}/gennotused/${m}
    )
endforeach()

foreach(f ttt.txt opcode_def.h vxi.h)
  select_file(src ${f})
  list(APPEND tttfiles ${src})
endforeach()
set(mumps_ttt_args -run tttgen ${tttfiles})
add_custom_command(
  OUTPUT gennotused/ttt.c
  DEPENDS ${tttfiles}
    gennotused/chk2lev.m gennotused/chkop.m gennotused/gendash.m gennotused/genout.m gennotused/loadop.m
    gennotused/loadvx.m gennotused/tttgen.m gennotused/tttscan.m
    ${YDB_SOURCE_DIR}/sr_unix/mumps.cmake ${gen_bootstrap_depend}
  WORKING_DIRECTORY ${YDB_BINARY_DIR}/gennotused
  COMMAND ${CMAKE_COMMAND}
    -D ydb_dist=${gen_bootstrap_dist}
    -D gtm_tools=${YDB_SOURCE_DIR}/sr_port
    -D ydb_routines=.
    -D mumps=${gen_bootstrap_mumps}
    -D "args=${mumps_ttt_args}"
    -D output_file=ttt.log
    -P ${YDB_SOURCE_DIR}/sr_unix/mumps.cmake
  VERBATIM
  )
list(APPEND gen_bootstrap_files gennotused/ttt.c)

set(gen_merrors_extra gennotused/merrors_ansi.h)
foreach(msg
    sr_port/cmerrors.msg
    sr_port/gdeerrors.msg
    sr_port/merrors.msg
    sr_port/cmierrors.msg
    )
  get_filename_component(name ${msg} NAME_WE)
  set(mumps_msg_args -run msg ${YDB_SOURCE_DIR}/${msg} unix)
  set(outputs gennotused/${name}_ctl.c ${gen_${name}_extra})
  add_custom_command(
    OUTPUT ${outputs}
    DEPENDS gennotused/msg.m ${YDB_SOURCE_DIR}/${msg}
            ${YDB_SOURCE_DIR}/sr_unix/mumps.cmake ${gen_bootstrap_depend}
    WORKING_DIRECTORY ${YDB_BINARY_DIR}/gennotused
    COMMAND ${CMAKE_COMMAND}
      -D ydb_dist=${gen_bootstrap_dist}
      -D gtm_tools=${YDB_SOURCE_DIR}/sr_port
      -D ydb_routines=.
      -D mumps=${gen_bootstrap_mumps}
      -D "args=${mumps_msg_args}"
      -P ${YDB_SOURCE_DIR}/sr_unix/mumps.cmake
    VERBATIM
    )
  list(APPEND gen_bootstrap_files ${outputs})
endforeach()

add_custom_target(gen_bootstrap ALL DEPENDS ${gen_bootstrap_files})

#-----------------------------------------------------------------------------

if(gen_xfer_desc)
  list(SORT sources_used)
  set(CMAKE_CONFIGURABLE_FILE_CONTENT "")
  foreach(src ${sources_used})
    set(CMAKE_CONFIGURABLE_FILE_CONTENT "${CMAKE_CONFIGURABLE_FILE_CONTENT}${src}\n")
  endforeach()
  configure_file(${CMAKE_ROOT}/Modules/CMakeConfigurableFile.in ${YDB_BINARY_DIR}/sources.list)
  add_custom_command(
    OUTPUT xfer_desc.i
    WORKING_DIRECTORY ${YDB_BINARY_DIR}
    DEPENDS ${YDB_SOURCE_DIR}/sr_unix/gen_xfer_desc.cmake
            ${YDB_BINARY_DIR}/sources.list
    COMMAND ${CMAKE_COMMAND}
      -D CMAKE_C_COMPILER=${CMAKE_C_COMPILER}
      -D "arch=${arch}"
      -D "includes=${includes}"
      -P ${YDB_SOURCE_DIR}/sr_unix/gen_xfer_desc.cmake
    VERBATIM
    )
  add_custom_target(gen_xfer_desc DEPENDS xfer_desc.i)
  if(gen_bootstrap)
    add_dependencies(gen_xfer_desc gen_bootstrap)
  endif()
endif()

#-----------------------------------------------------------------------------

add_executable(gtm_threadgbl_deftypes ${gtm_threadgbl_deftypes_SOURCES})
if(gen_xfer_desc)
  add_dependencies(gtm_threadgbl_deftypes gen_xfer_desc)
endif()

# gtm_threadgbl_deftypes
set(gtmthreadgblasmhdr   "genused/gtm_threadgbl_deftypes.h")
add_custom_command(
  OUTPUT ${gtmthreadgblasmhdr}
  DEPENDS gtm_threadgbl_deftypes
  COMMAND gtm_threadgbl_deftypes > ${gtmthreadgblasmhdr}
  )
# gtm_threadgbl_deftypes_asm.si
set(gtmthreadgblasmfile   "genused/gtm_threadgbl_deftypes_asm.si")
select_file(gtmthreadgblasmaccess "gtm_threadgbl_asm_access.txt")
add_custom_command(
  OUTPUT ${gtmthreadgblasmfile}
  DEPENDS ${gtmthreadgblasmhdr}
  COMMAND ${CMAKE_COMMAND}
    -D "gtmthreadgblasmfile=${gtmthreadgblasmfile}"
    -D "gtmthreadgblasmhdr=${gtmthreadgblasmhdr}"
    -D "gtmthreadgblasmaccess=${gtmthreadgblasmaccess}"
    -P ${YDB_SOURCE_DIR}/${srdir}/gen_threadgbl_asm.cmake
  VERBATIM
  )

add_custom_target(gen_gtm_threadgbl_deftypes
  DEPENDS ${YDB_BINARY_DIR}/${gtmthreadgblasmhdr} ${YDB_BINARY_DIR}/${gtmthreadgblasmfile})

foreach(exp ydbexe yottadb)
  set(out ${exp}_symbols.export)
  set(in ${YDB_SOURCE_DIR}/sr_unix/${exp}_symbols.exp)

  # Add underscores to symbols for Mac OS X
  if("${CMAKE_SYSTEM_NAME}" MATCHES "Darwin")
    add_custom_command(
      OUTPUT ${out}
      DEPENDS ${in} gen_gtm_threadgbl_deftypes
      COMMAND tcsh -f ${YDB_SOURCE_DIR}/sr_linux/genexport.csh ${in} ${out}
      COMMAND sed 's/^/_/' ${in} > ${out}
      )
  else()
    add_custom_command(
      OUTPUT ${out}
      DEPENDS ${in} gen_gtm_threadgbl_deftypes
      COMMAND tcsh -f ${YDB_SOURCE_DIR}/sr_linux/genexport.csh ${in} ${out}
      )
  endif()
endforeach()

add_custom_target(gen_export DEPENDS ydbexe_symbols.export yottadb_symbols.export)

foreach(lib
  mumps
  ${extralibs}
  )
  add_library(lib${lib} STATIC ${lib${lib}_SOURCES})
  set_property(TARGET lib${lib} PROPERTY OUTPUT_NAME ${lib})
  add_dependencies(lib${lib} gen_gtm_threadgbl_deftypes)
endforeach()

# TODO: find_package or find_library for system libs?
include_directories (/usr/local/include)
target_link_libraries(libmumps ${libsyslibs})
if(gen_xfer_desc)
  add_dependencies(libmumps gen_xfer_desc)
endif()

function(add_executable target)
  _add_executable(${target} ${ARGN})
  if("${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
    set_target_properties(${target} PROPERTIES LINK_FLAGS "-Wl,-map,${target}.map")
  elseif("${CMAKE_C_COMPILER_ID}" MATCHES "GNU")
    set_target_properties(${target} PROPERTIES LINK_FLAGS "-Wl,-Map=${target}.map")
  endif()
  # Since all utilities now dlopen() libyottadb.so at startup, add a dependency to not build the utilities
  # until libyottadb.so was built. And that in turn relies on threadgbl_deftypes being built so add that too.
  add_dependencies(${target} libyottadb gen_gtm_threadgbl_deftypes)
  # And add system libraries as a target library for all utilities (e.g. dlopen() etc.).
  target_link_libraries(${target} ${libsyslibs})
endfunction()
add_executable(mumps ${mumps_SOURCES})

add_executable(dse ${dse_SOURCES})
list(APPEND with_export dse)

add_executable(dbcertify ${dbcertify_SOURCES})

add_executable(gtmsecshr ${gtmsecshr_SOURCES})
target_link_libraries(gtmsecshr libmumps) # needed for static linking since this is a setuid executable
                                          # and does not do a dlopen of libyottadb.so

add_executable(gtmsecshr_real ${gtmsecshr_real_SOURCES})
set_target_properties(gtmsecshr_real PROPERTIES
  OUTPUT_NAME gtmsecshr
  RUNTIME_OUTPUT_DIRECTORY ${YDB_BINARY_DIR}/gtmsecshrdir
  )
target_link_libraries(gtmsecshr_real libmumps) # needed for static linking since this is a setuid executable
                                               # and does not do a dlopen of libyottadb.so

add_executable(mupip ${mupip_SOURCES})
list(APPEND with_export mupip)

add_executable(lke ${lke_SOURCES})
list(APPEND with_export lke)


add_executable(gtcm_server ${gtcm_server_SOURCES})
list(APPEND with_export gtcm_server)

add_executable(gtcm_gnp_server ${gtcm_gnp_server_SOURCES})
list(APPEND with_export gtcm_gnp_server)

add_executable(gtcm_play ${gtcm_play_SOURCES})
list(APPEND with_export gtcm_play)

add_executable(gtcm_pkdisp ${gtcm_pkdisp_SOURCES})

add_executable(gtcm_shmclean ${gtcm_shmclean_SOURCES})

add_executable(semstat2 ${semstat2_SOURCES})

add_executable(ftok ${ftok_SOURCES})

add_library(libyottadb MODULE ${libyottadb_SOURCES})
set_property(TARGET libyottadb PROPERTY OUTPUT_NAME yottadb)

if("${CMAKE_SYSTEM_NAME}" MATCHES "Darwin")
  set_target_properties(libyottadb PROPERTIES SUFFIX ".dylib")
endif()
target_link_libraries(libyottadb libmumps)
if("${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
  set_target_properties(libyottadb PROPERTIES
  LINK_FLAGS "${libyottadb_link} -Wl,-map,libyottadb.map"
  LINK_DEPENDS "${libyottadb_dep}"
  )
elseif("${CMAKE_C_COMPILER_ID}" MATCHES "GNU")
  set_target_properties(libyottadb PROPERTIES
  LINK_FLAGS "${libyottadb_link} -Wl,-Map=libyottadb.map"
  LINK_DEPENDS "${libyottadb_dep}"
  )
endif()
add_dependencies(libyottadb gen_export)
add_dependencies(mumps libyottadb)

# Iterate over the list of GPG related libraries
foreach(gpglib gpg-error gpgme gcrypt config)
  # For each library, we need a new CMake variable, hence GPGLIB_${gpglib}
  find_library(GPGLIB_${gpglib} NAME ${gpglib} PATHS ${CMAKE_LIBRARY_PATH})
  # Append the found library to the list
  set(GPG_LIBRARIES ${GPG_LIBRARIES} ${GPGLIB_${gpglib}})
endforeach()

# Iterate over the list of SSL related libraries
foreach(tlslib ssl crypto config)
  # For each library, we need a new CMake variable, hence TLSLIB_${tlslib}
  find_library(TLSLIB_${tlslib} NAME ${tlslib} PATHS ${CMAKE_LIBRARY_PATH})
  # Append the found library to the list
  set(TLS_LIBRARIES ${TLS_LIBRARIES} ${TLSLIB_${tlslib}})
endforeach()

# Building the three encryption libraries could by a loop of some sort, but
# manually creating each target is way easier.

# Library=GCRYPT Algorithm=AES256CFB
include_directories (/usr/local/ssl/include)
add_library(libgtmcrypt_gcrypt_AES256CFB MODULE ${libgtmcrypt_SOURCES})
set_target_properties(libgtmcrypt_gcrypt_AES256CFB PROPERTIES
  OUTPUT_NAME gtmcrypt_gcrypt_AES256CFB
  COMPILE_DEFINITIONS "USE_GCRYPT -DUSE_AES256CFB"
  LIBRARY_OUTPUT_DIRECTORY ${YDB_BINARY_DIR}/plugin
  )
if("${CMAKE_SYSTEM_NAME}" STREQUAL "Darwin")
  set_target_properties(libgtmcrypt_gcrypt_AES256CFB PROPERTIES SUFFIX ".dylib")
else()
  set_target_properties(libgtmcrypt_gcrypt_AES256CFB PROPERTIES SUFFIX ".so")
endif()
add_dependencies(libgtmcrypt_gcrypt_AES256CFB gen_gtm_threadgbl_deftypes)
target_link_libraries(libgtmcrypt_gcrypt_AES256CFB ${GPG_LIBRARIES})
install(TARGETS libgtmcrypt_gcrypt_AES256CFB DESTINATION ${YDB_INSTALL_DIR}/plugin)

# Establish the default encryption link
if("${CMAKE_SYSTEM_NAME}" STREQUAL "Darwin")
  set(default_encr_link   ${YDB_BINARY_DIR}/plugin/libgtmcrypt.dylib)
else()
  set(default_encr_link   ${YDB_BINARY_DIR}/plugin/libgtmcrypt.so)
endif()
set(default_encr_target libgtmcrypt_gcrypt_AES256CFB)
add_custom_command(
  OUTPUT ${default_encr_link}
  COMMAND ${CMAKE_COMMAND} -E create_symlink "${default_encr_target}" "${default_encr_link}"
  DEPENDS ${YDB_BINARY_DIR}/plugin/${default_encr_target}
  COMMENT "Generating default_encr symbolic link"
)
add_custom_target(install_default_encr ALL DEPENDS ${default_encr_link})
install(FILES ${default_encr_link} DESTINATION ${YDB_INSTALL_DIR}/plugin)

# Library=OPENSSL Algorithm=AES256CFB
add_library(libgtmcrypt_openssl_AES256CFB MODULE ${libgtmcrypt_SOURCES})
set_target_properties(libgtmcrypt_openssl_AES256CFB PROPERTIES
  OUTPUT_NAME gtmcrypt_openssl_AES256CFB
  COMPILE_DEFINITIONS "USE_OPENSSL -DUSE_AES256CFB"
  LIBRARY_OUTPUT_DIRECTORY ${YDB_BINARY_DIR}/plugin
  )
if("${CMAKE_SYSTEM_NAME}" MATCHES "Darwin")
  set_target_properties(libgtmcrypt_openssl_AES256CFB PROPERTIES SUFFIX ".dylib")
else()
  set_target_properties(libgtmcrypt_openssl_AES256CFB PROPERTIES SUFFIX ".so")
endif()
target_link_libraries(libgtmcrypt_openssl_AES256CFB ${GPG_LIBRARIES} ${TLS_LIBRARIES})
add_dependencies(libgtmcrypt_openssl_AES256CFB gen_gtm_threadgbl_deftypes)
install(TARGETS libgtmcrypt_openssl_AES256CFB DESTINATION ${YDB_INSTALL_DIR}/plugin)

add_library(libgtmtls MODULE ${libgtmtls_SOURCES})
set_target_properties(libgtmtls PROPERTIES
  OUTPUT_NAME gtmtls
  COMPILE_DEFINITIONS "USE_OPENSSL"
  LIBRARY_OUTPUT_DIRECTORY ${YDB_BINARY_DIR}/plugin
  )
if("${CMAKE_SYSTEM_NAME}" MATCHES "Darwin")
  set_target_properties(libgtmtls PROPERTIES SUFFIX ".dylib")
else()
  set_target_properties(libgtmtls PROPERTIES SUFFIX ".so")
endif()
target_link_libraries(libgtmtls ${TLS_LIBRARIES})
add_dependencies(libgtmtls gen_gtm_threadgbl_deftypes)
install(TARGETS libgtmtls DESTINATION ${YDB_INSTALL_DIR}/plugin)

add_executable(maskpass ${maskpass_SOURCES})
target_link_libraries(maskpass ${GPG_LIBRARIES} ${TLS_LIBRARIES})
set_target_properties(maskpass PROPERTIES
  COMPILE_DEFINITIONS "USE_GCRYPT -DUSE_SYSLIB_FUNCS"
  RUNTIME_OUTPUT_DIRECTORY ${YDB_BINARY_DIR}/plugin/gtmcrypt
  )
install(TARGETS maskpass DESTINATION ${YDB_INSTALL_DIR}/plugin/gtmcrypt)

# Always copy files into the plugin directory
foreach(f_in
      ${YDB_SOURCE_DIR}/sr_unix/Makefile.mk
      ${YDB_SOURCE_DIR}/sr_unix/encrypt_sign_db_key.sh
      ${YDB_SOURCE_DIR}/sr_unix/gen_keypair.sh
      ${YDB_SOURCE_DIR}/sr_unix/gen_sym_hash.sh
      ${YDB_SOURCE_DIR}/sr_unix/gen_sym_key.sh
      ${YDB_SOURCE_DIR}/sr_unix/gtm_tls_impl.c
      ${YDB_SOURCE_DIR}/sr_unix/gtm_tls_impl.h
      ${YDB_SOURCE_DIR}/sr_unix/gtm_tls_interface.h
      ${YDB_SOURCE_DIR}/sr_unix/gtmcrypt_dbk_ref.c
      ${YDB_SOURCE_DIR}/sr_unix/gtmcrypt_dbk_ref.h
      ${YDB_SOURCE_DIR}/sr_unix/gtmcrypt_interface.h
      ${YDB_SOURCE_DIR}/sr_unix/gtmcrypt_pk_ref.c
      ${YDB_SOURCE_DIR}/sr_unix/gtmcrypt_pk_ref.h
      ${YDB_SOURCE_DIR}/sr_unix/gtmcrypt_ref.c
      ${YDB_SOURCE_DIR}/sr_unix/gtmcrypt_ref.h
      ${YDB_SOURCE_DIR}/sr_unix/gtmcrypt_sym_ref.c
      ${YDB_SOURCE_DIR}/sr_unix/gtmcrypt_sym_ref.h
      ${YDB_SOURCE_DIR}/sr_unix/gtmcrypt_util.c
      ${YDB_SOURCE_DIR}/sr_unix/gtmcrypt_util.h
      ${YDB_SOURCE_DIR}/sr_unix/import_and_sign_key.sh
      ${YDB_SOURCE_DIR}/sr_unix/maskpass.c
      ${YDB_SOURCE_DIR}/sr_port/minimal_gbldefs.c
      ${YDB_SOURCE_DIR}/sr_unix/pinentry-gtm.sh
      ${YDB_SOURCE_DIR}/sr_unix/pinentry.m
      ${YDB_SOURCE_DIR}/sr_unix/show_install_config.sh
      ${YDB_SOURCE_DIR}/sr_port/ydb_getenv.c
      ${YDB_SOURCE_DIR}/sr_port/ydb_getenv.h
      ${YDB_SOURCE_DIR}/sr_port/ydb_logicals.h
      ${YDB_SOURCE_DIR}/sr_port/ydb_logicals_tab.h
    )
  get_filename_component(f ${f_in} NAME)
  string(REGEX REPLACE ".mk$" "" f_mod "${f}")
  set(f_out "${YDB_BINARY_DIR}/plugin/gtmcrypt/${f_mod}")
  add_custom_command(
    OUTPUT "${f_out}"
    DEPENDS "${f_in}"
    COMMAND ${CMAKE_COMMAND} -E copy "${f_in}" "${f_out}"
    )
  if("${f}" MATCHES "\\.sh$")
    set(permissions PERMISSIONS ${install_permissions_script})
  else()
    set(permissions "")
  endif()
  install(FILES "${f_out}" DESTINATION ${YDB_INSTALL_DIR}/plugin/gtmcrypt ${permissions})
  list(APPEND files_to_place "${f_out}")
endforeach()

install(TARGETS
  mumps
  dse
  lke
  gtcm_server
  gtcm_gnp_server
  gtcm_pkdisp
  gtcm_play
  gtcm_shmclean
  semstat2
  ftok
  gtmsecshr
  mupip
  libyottadb
  DESTINATION ${YDB_INSTALL_DIR}
  )
install(TARGETS gtmsecshr_real DESTINATION ${YDB_INSTALL_DIR}/gtmsecshrdir)

# <r>.m -> <R>.m
#file(GLOB m_files_sr_port RELATIVE ${YDB_SOURCE_DIR}/sr_port ${YDB_SOURCE_DIR}/sr_port/*.m)
set(m_files_sr_port
  gde.m
  gdeadd.m
  gdechang.m
  gdedelet.m
  gdeexit.m
  gdehelp.m
  gdeinit.m
  gdeinitsz.m
  gdelocks.m
  gdelog.m
  gdemap.m
  gdemsgin.m
  gdeparse.m
  gdequit.m
  gderenam.m
  gdescan.m
  gdesetgd.m
  gdeshow.m
  gdespawn.m
  gdetempl.m
  )
file(GLOB m_files_sr_unix RELATIVE ${YDB_SOURCE_DIR}/sr_unix ${YDB_SOURCE_DIR}/sr_unix/*.m)
file(GLOB m_files_sr_x86_64 RELATIVE ${YDB_SOURCE_DIR}/sr_x86_64 ${YDB_SOURCE_DIR}/sr_x86_64/*.m)
file(GLOB m_files_sr_armv7l RELATIVE ${YDB_SOURCE_DIR}/sr_armv7l ${YDB_SOURCE_DIR}/sr_armv7l/*.m)
file(GLOB m_files_sr_i386 RELATIVE ${YDB_SOURCE_DIR}/sr_i386 ${YDB_SOURCE_DIR}/sr_i386/*.m)
file(GLOB mpt_files_sr_port RELATIVE ${YDB_SOURCE_DIR}/sr_port ${YDB_SOURCE_DIR}/sr_port/*.mpt)
file(GLOB mpt_files_sr_unix RELATIVE ${YDB_SOURCE_DIR}/sr_unix ${YDB_SOURCE_DIR}/sr_unix/*.mpt)

set(ydb_chset_dir_ "")
set(ydb_chset_dir_UTF-8 "/utf8")

foreach(d ${gt_src_list})
  foreach(m ${m_files_${d}})
    get_filename_component(m_name "${m}" NAME_WE)
    string(TOUPPER "${m_name}" m_upper)
    if("${m}" MATCHES "^gde")
      foreach(ydb_chset "" "UTF-8")
        set(m_out "${YDB_BINARY_DIR}${ydb_chset_dir_${ydb_chset}}/${m_upper}.m")
        add_custom_command(
          OUTPUT "${m_out}"
          DEPENDS ${YDB_SOURCE_DIR}/${d}/${m}
          COMMAND ${CMAKE_COMMAND} -E copy ${YDB_SOURCE_DIR}/${d}/${m} "${m_out}"
          )
        list(APPEND files_to_place "${m_out}")
        list(APPEND m_routines "${m_out}")
        list(APPEND ydb_chset_routines_${ydb_chset} "${m_out}")
      endforeach()
    else()
      if(";${m_name};" MATCHES ";GTMDefinedTypesInit[a-zA-Z]*;")
        set(m_out "${YDB_BINARY_DIR}/${m_name}.m")
        set(${m_name}_found 1)
      else()
        set(m_out "${YDB_BINARY_DIR}/${m_upper}.m")
      endif()
      add_custom_command(
        OUTPUT "${m_out}"
        DEPENDS ${YDB_SOURCE_DIR}/${d}/${m}
        COMMAND ${CMAKE_COMMAND} -E copy ${YDB_SOURCE_DIR}/${d}/${m} "${m_out}"
        )
      install(FILES "${m_out}" DESTINATION ${YDB_INSTALL_DIR})
      list(APPEND files_to_place "${m_out}")
      list(APPEND m_routines "${m_out}")
    endif()
  endforeach()
  foreach(m ${mpt_files_${d}})
    string(TOUPPER "_${m}" m_upper)
    string(REGEX REPLACE "MPT$" "m" m_out "${YDB_BINARY_DIR}/${m_upper}")
    add_custom_command(
      OUTPUT "${m_out}"
      DEPENDS ${YDB_SOURCE_DIR}/${d}/${m}
      COMMAND ${CMAKE_COMMAND} -E copy ${YDB_SOURCE_DIR}/${d}/${m} "${m_out}"
      )
    install(FILES "${m_out}" DESTINATION ${YDB_INSTALL_DIR})
    list(APPEND files_to_place "${m_out}")
    list(APPEND m_routines "${m_out}")
  endforeach()
endforeach()

set(files)
foreach(f
  gtm_common_defs.h
  gtm_descript.h
  gtm_limits.h
  gtm_sizeof.h
  gtm_stdio.h
  gtm_stdlib.h
  gtm_string.h
  gtm_strings.h
  gtmgblstat.xc
  gtmxc_types.h
  libyottadb.h
  libydberrors.h
  libydberrors2.h
  main_pragma.h
  arch.gtc
  gtcm_run.gtc
  gtcm_slist.gtc
  gdedefaults.gtc
  ydb_env_set.gtc
  ydb_env_unset.gtc
  dse.hlp
  gde.hlp
  lke.hlp
  mumps.hlp
  mupip.hlp
  custom_errors_sample.txt
  )
  select_file(src ${f})
  get_filename_component(srcfname "${src}" NAME)
  add_custom_command(
    OUTPUT "${YDB_BINARY_DIR}/${srcfname}"
    DEPENDS "${src}"
    COMMAND ${CMAKE_COMMAND} -E copy "${src}" "${YDB_BINARY_DIR}/${srcfname}"
    )
  list(APPEND files_to_place "${YDB_BINARY_DIR}/${srcfname}")
  list(APPEND files ${src})
endforeach()
install(FILES ${files} DESTINATION ${YDB_INSTALL_DIR})

set(scripts)
foreach(f
  ydb.gtc
  )
  select_file(src ${f})
  list(APPEND scripts ${src})
endforeach()
install(FILES ${scripts}
  DESTINATION ${YDB_INSTALL_DIR}
  PERMISSIONS ${install_permissions_script}
  )

find_program(ICUCONFIG NAMES icu-config)
if(ICUCONFIG)
  execute_process(
    COMMAND ${ICUCONFIG} --version
    OUTPUT_VARIABLE icu_version
    RESULT_VARIABLE icu_failed
    OUTPUT_STRIP_TRAILING_WHITESPACE
    )
  if(icu_failed)
    message(FATAL_ERROR "Command\n ${ICUCONFIG} --version\nfailed (${icu_failed}).")
  elseif("x${icu_version}" MATCHES "^x([0-9]+\\.[0-9]+)")
    set(ydb_icu_version "${CMAKE_MATCH_1}")
  else()
    message(FATAL_ERROR "Command\n ${ICUCONFIG} --version\nproduced unrecognized output:\n ${icu_version}")
  endif()
else()
  message(FATAL_ERROR "Unable to find 'icu-config'.  Set ICUCONFIG in CMake cache.")
endif()

find_program(LOCALECFG NAMES locale)
if(LOCALECFG)
  execute_process(
  COMMAND ${LOCALECFG} -a
    OUTPUT_VARIABLE locale_list
    RESULT_VARIABLE locale_failed
    OUTPUT_STRIP_TRAILING_WHITESPACE
    )
  if(locale_failed)
    message(FATAL_ERROR "Command\n ${LOCALECFG} -a\nfailed (${locale_failed}).")
  endif()
  STRING(REGEX REPLACE "\n" ";" locale_list "${locale_list}")
  foreach(lc ${locale_list})
    string(TOLOWER "${lc}" lc_lower)
    if("x${lc_lower}" MATCHES "^x[a-zA-Z_]+\\.?utf-?8")
      set(LC_ALL ${lc})
      message("-- Setting locale to ${LC_ALL}")
      break()
    endif()
  endforeach(lc)
  if("${LC_ALL}" STREQUAL "")
    message("Locale undefined. Expect to see NONUTF8LOCALE during MUMPS routine compilation: ${locale_list}\n")
  endif()
else()
  message(FATAL_ERROR "Unable to find 'locale'.  Set LOCALECFG in CMake cache.")
endif()

foreach(ydb_chset "" "UTF-8")
  foreach(in ${ydb_chset_routines_${ydb_chset}})
    string(REPLACE ".m" ".o" out "${in}")
    get_filename_component(out_dir "${out}" PATH)
    add_custom_command(
      OUTPUT ${out}
      DEPENDS ${in} ${YDB_SOURCE_DIR}/sr_unix/mumps.cmake mumps
      COMMAND ${CMAKE_COMMAND}
        -D ydb_dist=${YDB_BINARY_DIR}
        -D ydb_routines=.
        -D ydb_chset=${ydb_chset}
        -D ydb_icu_version=${ydb_icu_version}
        -D LC_ALL=${LC_ALL}
        -D mumps=$<TARGET_FILE:mumps>
        -D "args=${in}"
        -P ${YDB_SOURCE_DIR}/sr_unix/mumps.cmake
      WORKING_DIRECTORY ${out_dir}
      VERBATIM
      )
    list(APPEND files_to_place ${out})
    install(FILES "${out}" DESTINATION ${YDB_INSTALL_DIR}${ydb_chset_dir_${ydb_chset}})
  endforeach()
endforeach()

#-----------------------------------------------------------------------------
set(gtm_hlp mumps.hlp)
set(gde_hlp gde.hlp)
set(mupip_hlp mupip.hlp)
set(dse_hlp dse.hlp)
set(lke_hlp lke.hlp)
foreach(help gtm gde mupip dse lke)
  set(CMAKE_CONFIGURABLE_FILE_CONTENT
    "Change -segment DEFAULT -block=2048 -file=\$ydb_dist/${help}help.dat
Change -region DEFAULT -record=1020 -key=255 -qdbrundown -nostats
exit")
  configure_file(${CMAKE_ROOT}/Modules/CMakeConfigurableFile.in
                 ${YDB_BINARY_DIR}/${help}help.in1)
  set(CMAKE_CONFIGURABLE_FILE_CONTENT
    "Do ^GTMHLPLD
${YDB_SOURCE_DIR}/sr_port/${${help}_hlp}
Halt")
  configure_file(${CMAKE_ROOT}/Modules/CMakeConfigurableFile.in
                 ${YDB_BINARY_DIR}/${help}help.in2)
  set(env
    -D ydb_dist=${YDB_BINARY_DIR}
    -D ydb_routines=.
    -D ydb_gbldir=${YDB_BINARY_DIR}/${help}help
    )
  add_custom_command(
    OUTPUT ${help}help.dat ${help}help.gld
    DEPENDS ${YDB_SOURCE_DIR}/sr_unix/mumps.cmake mumps mupip
            ${YDB_BINARY_DIR}/${help}help.in1
            ${YDB_BINARY_DIR}/${help}help.in2
            ${m_routines}
    COMMAND ${CMAKE_COMMAND} -E remove ${help}help.dat ${help}help.gld
    COMMAND ${CMAKE_COMMAND} ${env}
      -D mumps=$<TARGET_FILE:mumps> -D "args=-run;GDE"
      -D input_file=${YDB_BINARY_DIR}/${help}help.in1
      -P ${YDB_SOURCE_DIR}/sr_unix/mumps.cmake
    COMMAND ${CMAKE_COMMAND} ${env}
      -D mumps=$<TARGET_FILE:mupip> -D "args=create"
      -P ${YDB_SOURCE_DIR}/sr_unix/mumps.cmake
    COMMAND ${CMAKE_COMMAND} ${env}
      -D mumps=$<TARGET_FILE:mumps> -D "args=-direct"
      -D input_file=${YDB_BINARY_DIR}/${help}help.in2
      -P ${YDB_SOURCE_DIR}/sr_unix/mumps.cmake
    VERBATIM
    )
  list(APPEND files_to_place ${help}help.dat)
  install(FILES ${YDB_BINARY_DIR}/${help}help.dat DESTINATION ${YDB_INSTALL_DIR})
  install(FILES ${YDB_BINARY_DIR}/${help}help.gld DESTINATION ${YDB_INSTALL_DIR})
endforeach()
if (GTMDefinedTypesInit${CMAKE_BUILD_TYPE}_found)
add_custom_command(
  OUTPUT GTMDEFINEDTYPESTODB.o
  DEPENDS gtmhelp.dat gtmhelp.gld GTMDefinedTypesInit${CMAKE_BUILD_TYPE}.m ${YDB_BINARY_DIR}/gtmhelp.dat_noro
  COMMAND ${CMAKE_COMMAND} -E copy GTMDefinedTypesInit${CMAKE_BUILD_TYPE}.m ${YDB_BINARY_DIR}/GTMDefinedTypesInit.m
  COMMAND ${CMAKE_COMMAND}
    -D ydb_dist=${YDB_BINARY_DIR}
    -D ydb_routines=.
    -D ydb_gbldir=${YDB_BINARY_DIR}/gtmhelp
    -D mumps=$<TARGET_FILE:mumps>
    -D "args=-run;GTMDEFINEDTYPESTODB"
    -P ${YDB_SOURCE_DIR}/sr_unix/mumps.cmake
    VERBATIM
  )
list(APPEND files_to_place GTMDEFINEDTYPESTODB.o)
endif(GTMDefinedTypesInit${CMAKE_BUILD_TYPE}_found)

foreach(help gtm gde mupip dse lke)
  set(CMAKE_CONFIGURABLE_FILE_CONTENT
    "SET -ACC=MM -READ_ONLY -reg \"*\"
exit")
  configure_file(${CMAKE_ROOT}/Modules/CMakeConfigurableFile.in
                 ${YDB_BINARY_DIR}/${help}help.in3)
  set(env
    -D ydb_dist=${YDB_BINARY_DIR}
    -D ydb_routines=.
    -D ydb_gbldir=${YDB_BINARY_DIR}/${help}help
    )
  add_custom_command(
    OUTPUT ${YDB_BINARY_DIR}/${help}help.dat_ro
    DEPENDS ${YDB_SOURCE_DIR}/sr_unix/mumps.cmake mumps mupip
            ${YDB_BINARY_DIR}/${help}help.in3
            ${help}help.dat ${help}help.gld ${help}help.dat_noro
    COMMAND ${CMAKE_COMMAND} ${env}
      -D mumps=$<TARGET_FILE:mupip>
      -D input_file=${YDB_BINARY_DIR}/${help}help.in3
      -P ${YDB_SOURCE_DIR}/sr_unix/mumps.cmake
    VERBATIM
    )
    list(APPEND targets_to_run ${YDB_BINARY_DIR}/${help}help.dat_ro)
endforeach()

foreach(help gtm gde mupip dse lke)
  set(CMAKE_CONFIGURABLE_FILE_CONTENT
    "SET -ACC=MM -NOREAD_ONLY -reg \"*\"
exit")
  configure_file(${CMAKE_ROOT}/Modules/CMakeConfigurableFile.in
                 ${YDB_BINARY_DIR}/${help}help.in4)
  set(env
    -D ydb_dist=${YDB_BINARY_DIR}
    -D ydb_routines=.
    -D ydb_gbldir=${YDB_BINARY_DIR}/${help}help
    )
  add_custom_command(
    OUTPUT ${YDB_BINARY_DIR}/${help}help.dat_noro
    DEPENDS ${YDB_SOURCE_DIR}/sr_unix/mumps.cmake mumps mupip
            ${YDB_BINARY_DIR}/${help}help.in4
            ${help}help.dat ${help}help.gld
    COMMAND ${CMAKE_COMMAND} ${env}
      -D mumps=$<TARGET_FILE:mupip>
      -D input_file=${YDB_BINARY_DIR}/${help}help.in4
      -P ${YDB_SOURCE_DIR}/sr_unix/mumps.cmake
    VERBATIM
    )
    list(APPEND targets_to_run ${YDB_BINARY_DIR}/${help}help.dat_noro)
endforeach()
#-----------------------------------------------------------------------------
configure_file(sr_unix/gpgagent.tab.in plugin/gpgagent.tab @ONLY)
set(YDB_TOP "$ENV{PWD}/${YDB_INSTALL_DIR}")
configure_file(sr_unix/gpgagent.tab.in CMakeFiles/plugin/gpgagent.tab @ONLY)
install(FILES ${YDB_BINARY_DIR}/CMakeFiles/plugin/gpgagent.tab DESTINATION ${YDB_INSTALL_DIR}/plugin)

install(FILES sr_unix/configure.gtc DESTINATION ${YDB_INSTALL_DIR}
  PERMISSIONS ${install_permissions_script}
  RENAME configure
  )

install(FILES sr_unix/ydbinstall.sh DESTINATION ${YDB_INSTALL_DIR}
  PERMISSIONS ${install_permissions_script}
  RENAME ydbinstall
  )

install(FILES sr_unix/lowerc_cp.sh DESTINATION ${YDB_INSTALL_DIR}
  RENAME lowerc_cp
  )

foreach(d COPYING README.md)
  if (EXISTS ${YDB_SOURCE_DIR}/${d})
    install(FILES ${d} DESTINATION ${YDB_INSTALL_DIR})
  endif()
endforeach()

add_custom_target(place_files ALL DEPENDS ${files_to_place})
add_custom_target(make_read_only ALL DEPENDS ${targets_to_run} place_files)

#-----------------------------------------------------------------------------
# Get the current build time and pass it as a macro to a C program (release_name.h)
string(TIMESTAMP BUILD_TIMESTAMP "%Y%m%d %H:%M")

# Make the build timestamp available to a C program (release_name.h)
add_definitions("-DYDB_RELEASE_STAMP=\"${BUILD_TIMESTAMP}\"")

