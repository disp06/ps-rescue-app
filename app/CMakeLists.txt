# VitaBootDiag.vpk
set(VITA_APP_NAME "VitaBootDiag")
set(VITA_TITLEID  "VBDG00001")
set(VITA_VERSION  "01.00")

add_executable(VitaBootDiag
  src/main.c
  src/gfx.c
  src/fsutil.c
  src/config.c
  src/detect.c
  src/errdict.c
  src/fixes.c
  src/report.c
)
target_include_directories(VitaBootDiag PRIVATE ${PROJECT_SOURCE_DIR}/common src)
target_compile_options(VitaBootDiag PRIVATE -O2 -Wall)
target_link_libraries(VitaBootDiag
  SceDisplay_stub
  SceCtrl_stub
  ScePower_stub
  SceRtc_stub
  SceVshBridge_stub
  SceSysmem_stub
  SceKernelModulemgr_stub
  SceIofilemgr_stub
)

vita_create_self(eboot.bin VitaBootDiag UNSAFE)
vita_create_vpk(VitaBootDiag.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE sce_sys/icon0.png sce_sys/icon0.png
       sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
       sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
       sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
       ${CMAKE_BINARY_DIR}/plugins/boot_logger_kernel/boot_logger.skprx plugins/boot_logger.skprx
       ${CMAKE_BINARY_DIR}/plugins/boot_logger_user/boot_logger.suprx plugins/boot_logger.suprx
)
# plugins must exist before they are packed into the VPK
add_dependencies(VitaBootDiag.vpk-vpk boot_logger.skprx-self boot_logger.suprx-self)
