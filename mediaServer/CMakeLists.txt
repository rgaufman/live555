add_executable(live555MediaServer
    DynamicRTSPServer.cpp
    DynamicRTSPServer.hh
    live555MediaServer.cpp
    version.hh
)
target_link_libraries(live555MediaServer PRIVATE live555::live555)
set_target_properties(live555MediaServer PROPERTIES FOLDER "live555/Servers")

target_include_directories(live555MediaServer
PUBLIC
    $<BUILD_INTERFACE:${live555_SOURCE_DIR}/liveMedia/include>
)
if ( LIVE555_MONOLITH_BUILD )
	message(STATUS "build monolith library on live555MediaServer.")
    if ( MSVC )
        add_definitions(-DLIVEMEDIA_API=__declspec\(dllimport\))
    else ()
        add_definitions(-DLIVEMEDIA_API=)
    endif ()
else ()
    add_definitions(-DLIVEMEDIA_API=)
# 	target_compile_definitions(live555MediaServer PRIVATE LIVEMEDIA_STATIC=1)
# 	message(STATUS "build static library on live555MediaServer")
endif ()

# if ( MSVC )
#     add_definitions(/FI"export.h")
# else()
#     # GCC or Clang
#     add_definitions(-include export.h)
# endif()


# if ( WIN32 )
#     set ( LIB_EXTENSION lib )
# else ()
#     if( BUILD_SHARED_LIBS )
#         #For the shared library:
#         set ( LIB_EXTENSION so )
#     else()
#         #For the static library:
#         set ( LIB_EXTENSION a )
#     endif()
# endif()