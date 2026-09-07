#######################################
# Effect

set(geometrychange_SOURCES
    geometrychange.cpp
    main.cpp
)

kconfig_add_kcfg_files(geometrychange_SOURCES
    geometrychangeconfig.kcfgc
)

kwin_add_builtin_effect(geometrychange ${geometrychange_SOURCES})
target_link_libraries(geometrychange PRIVATE
    kwin

    KF6::ConfigGui
)

#######################################
# Config
if (KWIN_BUILD_KCMS)
    set(kwin_geometrychange_config_SRCS geometrychange_config.cpp)
    ki18n_wrap_ui(kwin_geometrychange_config_SRCS geometrychange_config.ui)
    kconfig_add_kcfg_files(kwin_geometrychange_config_SRCS geometrychangeconfig.kcfgc)

    kwin_add_effect_config(kwin_geometrychange_config ${kwin_geometrychange_config_SRCS})

    target_link_libraries(kwin_geometrychange_config
        KF6::KCMUtils
        KF6::CoreAddons
        KF6::I18n
        Qt::DBus
        KWinEffectsInterface
    )
endif()
