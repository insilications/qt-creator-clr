add_qtc_library(LanguageUtils STATIC
  DEFINES LANGUAGEUTILS_BUILD_STATIC_LIB
  PUBLIC_DEPENDS Qt5::Core
  SOURCES
    componentversion.cpp componentversion.h
    fakemetaobject.cpp fakemetaobject.h
    languageutils_global.h
)
