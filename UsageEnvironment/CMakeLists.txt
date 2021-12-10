set(headers
    include/Boolean.hh
    include/HashTable.hh
    include/UsageEnvironment.hh
    include/UsageEnvironment_version.hh
    include/strDup.hh
)
add_library(UsageEnvironment ${live555_library_type}
    ${headers}
    HashTable.cpp
    UsageEnvironment.cpp
    strDup.cpp
)
target_include_directories(UsageEnvironment
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        # This is a cyclic dependency :(
        $<BUILD_INTERFACE:${live555_SOURCE_DIR}/groupsock/include>

        $<BUILD_INTERFACE:${live555_SOURCE_DIR}/liveMedia/include>
)
target_link_libraries(UsageEnvironment PUBLIC live555_cxx_flags)
set_target_properties(UsageEnvironment PROPERTIES FOLDER "live555/lib")

if ( LIVE555_MONOLITH_BUILD )
	message(STATUS "build monolith library on UsageEnvironment.")
    if ( MSVC )
        add_definitions(-DLIVEMEDIA_API=__declspec\(dllexport\))
        # add_definitions(/FI"export.h")
        # target_compile_definitions(UsageEnvironment PRIVATE LIVEMEDIA_EXPORTS=1)
        # remove_definitions(-DLIVEMEDIA_STATIC)
        # remove_definitions(-DLIVEMEDIA_IMPORTS)
        # add_definitions(-DLIVEMEDIA_EXPORTS)
    else ()
        add_definitions(-DLIVEMEDIA_API=)
    endif ()
else ()
    add_definitions(-DLIVEMEDIA_API=)
	# target_compile_definitions(UsageEnvironment PRIVATE LIVEMEDIA_STATIC=1)
	# message(STATUS "build static library on UsageEnvironment")
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

if(NOT LIVE555_MONOLITH_BUILD)
    add_library(live555::UsageEnvironment ALIAS UsageEnvironment)
    live555_target_version(UsageEnvironment AUTO)
endif()

install(TARGETS UsageEnvironment EXPORT live555Target
    INCLUDES DESTINATION include/UsageEnvironment
    RUNTIME DESTINATION bin/$<CONFIG>
    LIBRARY DESTINATION lib/$<CONFIG>
    ARCHIVE DESTINATION lib/$<CONFIG>
)
install(FILES ${headers}
    DESTINATION include/UsageEnvironment
)
