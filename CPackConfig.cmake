# CPack configuration file for tint3.
# Based on compton's CPackConfig.cmake (https://github.com/chjj/compton).

# Environment
if(NOT CPACK_SYSTEM_NAME)
	set(CPACK_SYSTEM_NAME "${CMAKE_SYSTEM_PROCESSOR}")
	if(CPACK_SYSTEM_NAME STREQUAL "x86_64")
		set(CPACK_SYSTEM_NAME "amd64")
	endif()
endif()

# Build and install information
set(CPACK_INSTALL_CMAKE_PROJECTS "package-build;tint3;ALL;/")
set(CPACK_CMAKE_GENERATOR "Unix Makefiles")

# Basic information
set(CPACK_PACKAGE_NAME "tint3")
set(CPACK_PACKAGE_VENDOR "Daniele Cocca")
set(CPACK_PACKAGE_VERSION_MAJOR "1")
set(CPACK_PACKAGE_VERSION_MINOR "0")
set(CPACK_PACKAGE_VERSION_PATCH "0")
set(CPACK_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
set(CPACK_PACKAGE_DESCRIPTION "A lightweight X11 panel, taskbar and system tray. A backwards-compatible fork and C++ rewrite of tint2.")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A lightweight X11 panel")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_SYSTEM_NAME}")
set(CPACK_PACKAGE_CONTACT "https://github.com/jmc-88/tint3/issues")

# Package config
set(CPACK_GENERATOR "DEB" "RPM")
set(CPACK_RESOURCE_FILE_LICENSE "LICENSE")
set(CPACK_RESOURCE_FILE_README "README.md")
set(CPACK_STRIP_FILES 1)

# DEB package config
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_VENDOR} (${CPACK_PACKAGE_CONTACT})")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${CPACK_SYSTEM_NAME}")
set(CPACK_DEBIAN_PACKAGE_SECTION "x11")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libcairo2, libpango1.0-0, libglib2.0-0, libimlib2, libxinerama1, libx11-6, libxdamage1, libxcomposite1, libxrender1, libxrandr2, librsvg2-2, libstartup-notification0, libc++1")

# RPM package config
set(CPACK_RPM_PACKAGE_ARCHITECTURE "${CPACK_SYSTEM_NAME}")
set(CPACK_RPM_PACKAGE_LICENSE "GPL2")
set(CPACK_RPM_PACKAGE_REQUIRES "/bin/sh,libcairo.so.0,libpangocairo-1.0.so.0,,libpango-1.0.so.0,libgobject-2.0.so.0,libglib-2.0.so.0,libImlib2.so.1,libXinerama.so.1,libX11.so.6,libXdamage.so.1,libXcomposite.so.1,libXrender.so.1,libXrandr.so.2,librsvg-2.so.2,libstartup-notification-1.so.0,libc++.so.1")

# Source package config
set(CPACK_SOURCE_GENERATOR "DEB RPM")