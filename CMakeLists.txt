cmake_minimum_required(VERSION 3.15 FATAL_ERROR)

project(arch_afuc CXX)

if(NOT BN_API_PATH)
	set(BN_API_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../binaryninja-api"
	    CACHE PATH "Path to Binary Ninja API")
endif()

if(NOT BN_INTERNAL_BUILD)
	add_subdirectory(${BN_API_PATH} ${CMAKE_CURRENT_BINARY_DIR}/api)
endif()

file(GLOB SOURCES CONFIGURE_DEPENDS
	*.cpp
	*.h)

if(DEMO)
	add_library(arch_afuc STATIC ${SOURCES})
else()
	add_library(arch_afuc SHARED ${SOURCES})
endif()

target_include_directories(arch_afuc
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
	PRIVATE ${BN_API_PATH})

if(WIN32)
	target_link_directories(arch_afuc
		PRIVATE ${BN_INSTALL_DIR})
	target_link_libraries(arch_afuc binaryninjaapi binaryninjacore)
else()
	target_link_libraries(arch_afuc binaryninjaapi)
endif()

set_target_properties(arch_afuc PROPERTIES
	CXX_STANDARD 20
	CXX_VISIBILITY_PRESET hidden
	CXX_STANDARD_REQUIRED ON
	VISIBILITY_INLINES_HIDDEN ON
	POSITION_INDEPENDENT_CODE ON)

if(BN_INTERNAL_BUILD)
	plugin_rpath(arch_afuc)
	set_target_properties(arch_afuc PROPERTIES
		LIBRARY_OUTPUT_DIRECTORY ${BN_CORE_PLUGIN_DIR}
		RUNTIME_OUTPUT_DIRECTORY ${BN_CORE_PLUGIN_DIR})
endif()

# Installation: copy to BN plugins directory
if(NOT BN_INTERNAL_BUILD)
	# Determine plugin install directory
	if(APPLE)
		set(BN_PLUGIN_DIR "$ENV{HOME}/Library/Application Support/Binary Ninja/plugins")
	elseif(UNIX)
		set(BN_PLUGIN_DIR "$ENV{HOME}/.binaryninja/plugins")
	elseif(WIN32)
		set(BN_PLUGIN_DIR "$ENV{APPDATA}/Binary Ninja/plugins")
	endif()

	install(TARGETS arch_afuc
		LIBRARY DESTINATION "${BN_PLUGIN_DIR}"
		RUNTIME DESTINATION "${BN_PLUGIN_DIR}")
endif()
