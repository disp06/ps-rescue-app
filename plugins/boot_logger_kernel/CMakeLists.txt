# boot_logger.skprx  (kernel plugin, *KERNEL section)
add_executable(boot_logger_k main.c)
target_include_directories(boot_logger_k PRIVATE ${PROJECT_SOURCE_DIR}/common)
target_compile_options(boot_logger_k PRIVATE -Os -Wall -fno-builtin)
target_link_libraries(boot_logger_k
  SceSysclibForDriver_stub
  SceIofilemgrForDriver_stub
  SceModulemgrForKernel_stub
  SceThreadmgrForDriver_stub
  SceRtcForDriver_stub
  SceSysrootForDriver_stub
  gcc
)
set_target_properties(boot_logger_k PROPERTIES LINK_FLAGS "-nostdlib")
vita_create_self(boot_logger.skprx boot_logger_k
  CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/exports.yml UNSAFE)
