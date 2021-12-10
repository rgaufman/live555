set(headers
    include/BasicHashTable.hh
    include/BasicUsageEnvironment.hh
    include/BasicUsageEnvironment0.hh
    include/BasicUsageEnvironment_version.hh
    include/DelayQueue.hh
    include/HandlerSet.hh
)
add_library(BasicUsageEnvironment ${live555_library_type}
    ${headers}
    BasicHashTable.cpp
    BasicTaskScheduler.cpp
    BasicTaskScheduler0.cpp
    BasicUsageEnvironment.cpp
    BasicUsageEnvironment0.cpp
    DelayQueue.cpp
)
target_include_directories(BasicUsageEnvironment
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${live555_SOURCE_DIR}/liveMedia/include>
)

if ( LIVE555_MONOLITH_BUILD )
    message(STATUS "build monolith library on BasicUsageEnvironment.")
    if ( MSVC )
        add_definitions(-DLIVEMEDIA_API=__declspec\(dllexport\))
        # add_definitions(-DLIVEMEDIA_API=__declspec\(dllimport\))
    # add_definitions(/FI"export.h")
	    # target_compile_definitions(BasicUsageEnvironment PRIVATE LIVEMEDIA_EXPORTS -D LIVEMEDIA_IMPORTS)
        # target_remove_definitions(BasicUsageEnvironment PRIVATE LIVEMEDIA_STATIC)
        # add_definitions(-DLIVEMEDIA_EXPORTS)
    else ()
        add_definitions(-DLIVEMEDIA_API=)
    endif ()
else ()
    add_definitions(-DLIVEMEDIA_API=)
	# message(STATUS "build static library on BasicUsageEnvironment")
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

target_link_libraries(BasicUsageEnvironment PUBLIC
    live555_cxx_flags
    UsageEnvironment
)
set_target_properties(BasicUsageEnvironment PROPERTIES FOLDER "live555/lib")

if(NOT LIVE555_MONOLITH_BUILD)
    add_library(live555::BasicUsageEnvironment ALIAS BasicUsageEnvironment)
    live555_target_version(BasicUsageEnvironment AUTO)
endif()

install(TARGETS BasicUsageEnvironment EXPORT live555Target
    INCLUDES DESTINATION include/BasicUsageEnvironment
    RUNTIME DESTINATION bin/$<CONFIG>
    LIBRARY DESTINATION lib/$<CONFIG>
    ARCHIVE DESTINATION lib/$<CONFIG>
)
install(FILES ${headers}
    DESTINATION include/BasicUsageEnvironment
)
