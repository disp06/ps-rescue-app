# boot_logger.suprx  (user plugin, *main section = SceShell)
add_executable(boot_logger_u main.c)
target_include_directories(boot_logger_u PRIVATE ${PROJECT_SOURCE_DIR}/common)
target_compile_options(boot_logger_u PRIVATE -Os -Wall -fno-builtin)
target_link_libraries(boot_logger_u
  SceLibKernel_stub
  SceIofilemgr_stub
  SceKernelModulemgr_stub
  SceRtc_stub
  gcc
)
set_target_properties(boot_logger_u PROPERTIES LINK_FLAGS "-nostdlib")
vita_create_self(boot_logger.suprx boot_logger_u
  CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/exports.yml UNSAFE)
