# System installation rules for the Linux port.
# Respects CMAKE_INSTALL_PREFIX and DESTDIR (e.g. `cmake --install build --prefix /usr`).

include(GNUInstallDirs)

# systemd only reads /usr/lib/udev/rules.d (never lib64 on RPM systems), so
# keep this literal instead of ${CMAKE_INSTALL_LIBDIR}.
set(WB_UDEV_DIR "lib/udev/rules.d"
    CACHE PATH "Directory (relative to prefix) for the hidraw uaccess udev rule")

install(TARGETS kj-batteryhub
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    COMPONENT Runtime)

install(FILES "${PROJECT_SOURCE_DIR}/linux/kj-batteryhub.desktop"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
    COMPONENT Runtime)

install(FILES "${PROJECT_SOURCE_DIR}/linux/kj-batteryhub.svg"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
    RENAME "kj-batteryhub.svg"
    COMPONENT Runtime)

install(FILES "${PROJECT_SOURCE_DIR}/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
    RENAME "copyright"
    COMPONENT Runtime)

# 70-, not 99-: uaccess tags must be set before systemd's 73-seat-late.rules applies them.
install(FILES "${PROJECT_SOURCE_DIR}/linux/70-kj-batteryhub.rules"
    DESTINATION "${WB_UDEV_DIR}"
    COMPONENT Udev)
