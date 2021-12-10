set(headers
    include/GroupEId.hh
    include/Groupsock.hh
    include/GroupsockHelper.hh
    include/IOHandlers.hh
    include/NetAddress.hh
    include/NetCommon.h
    include/NetInterface.hh
    include/groupsock_version.hh
)
add_library(groupsock ${live555_library_type}
    ${headers}
    GroupEId.cpp
    Groupsock.cpp
    GroupsockHelper.cpp
    IOHandlers.cpp
    NetAddress.cpp
    NetInterface.cpp
    inet.c
)
target_include_directories(groupsock
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${live555_SOURCE_DIR}/liveMedia/include>
)

if ( LIVE555_MONOLITH_BUILD )
    message(STATUS "build monolith library on groupsock.")
    if ( MSVC )
        add_definitions(-DLIVEMEDIA_API=__declspec\(dllexport\))
		# add_definitions(/FI"export.h")
	    # target_compile_definitions(groupsock PRIVATE LIVEMEDIA_EXPORTS=1)
        # remove_definitions(-DLIVEMEDIA_STATIC)
        # remove_definitions(-DLIVEMEDIA_IMPORTS)
        # add_definitions(-DLIVEMEDIA_EXPORTS)
    else ()
        add_definitions(-DLIVEMEDIA_API=)
    endif ()
else ()
    add_definitions(-DLIVEMEDIA_API=)
	# target_compile_definitions(groupsock PRIVATE LIVEMEDIA_STATIC=1)
	# message(STATUS "build static library on groupsock")
endif ()

# if ( MSVC )
#     add_definitions(/FI"export.h")
# else()
#     # GCC or Clang
#     add_definitions(-include export.h)
# endif()

if ( WIN32 )
    set ( LIB_EXTENSION lib )
else ()
    if( BUILD_SHARED_LIBS )
        #For the shared library:
        set ( LIB_EXTENSION so )
    else()
        #For the static library:
        set ( LIB_EXTENSION a )
    endif()
endif()

target_link_libraries(groupsock
    PRIVATE live555_cxx_flags
    PUBLIC  UsageEnvironment
)
if(WIN32)
    target_link_libraries(groupsock PRIVATE ws2_32 mswsock)
    target_compile_definitions(groupsock PRIVATE NO_GETIFADDRS)
endif()
set_target_properties(groupsock PROPERTIES FOLDER "live555/lib")

if(NOT LIVE555_MONOLITH_BUILD)
    add_library(live555::groupsock ALIAS groupsock)
    live555_target_version(groupsock AUTO)
endif()

install(TARGETS groupsock EXPORT live555Target
    INCLUDES DESTINATION include/groupsock
    RUNTIME DESTINATION bin/$<CONFIG>
    LIBRARY DESTINATION lib/$<CONFIG>
    ARCHIVE DESTINATION lib/$<CONFIG>
)
install(FILES ${headers}
    DESTINATION include/groupsock
)
