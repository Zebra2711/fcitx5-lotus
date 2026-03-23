install(FILES fcitx5-lotus-server@.service
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system)

install(FILES 99-lotus.rules
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/udev/rules.d)

install(FILES module-fcitx5-lotus.conf
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/modules-load.d
    RENAME fcitx5-lotus.conf)

install(FILES user-lotus.conf
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/sysusers.d
    RENAME lotus.conf)

install(PROGRAMS fcitx5-lotus.openrc
    DESTINATION /etc/init.d
    RENAME fcitx5-lotus)
