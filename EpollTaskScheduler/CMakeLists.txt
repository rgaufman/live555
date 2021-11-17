set(headers
    include/EpollTaskScheduler.hh
)
add_library(EpollTaskScheduler ${live555_library_type}
    ${headers}
    EpollTaskScheduler.cpp
)
if(WIN32)
    target_sources(EpollTaskScheduler PRIVATE 
        wepoll.c 
        wepoll.h
    )
endif()
target_include_directories(EpollTaskScheduler
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${live555_SOURCE_DIR}/liveMedia/include>
)
target_link_libraries(EpollTaskScheduler PUBLIC
    live555_cxx_flags
    BasicUsageEnvironment
)

if ( BUILD_SHARED_LIBS )
	target_compile_definitions(EpollTaskScheduler PRIVATE LIVEMEDIA_EXPORTS=1)
	message(STATUS "build shared library on EpollTaskScheduler.")
else ()
	target_compile_definitions(EpollTaskScheduler PRIVATE LIVEMEDIA_STATIC=1)
	message(STATUS "build static library on EpollTaskScheduler")
endif ()
if ( MSVC )
    add_definitions(/FI"export.h")
else()
    # GCC or Clang
    add_definitions(-include export.h)
endif()


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

target_compile_definitions(EpollTaskScheduler INTERFACE HAVE_EPOLL_SCHEDULER)
set_target_properties(EpollTaskScheduler PROPERTIES FOLDER "live555/lib")

if(NOT LIVE555_MONOLITH_BUILD)
    add_library(live555::EpollTaskScheduler ALIAS EpollTaskScheduler)
    live555_target_version(EpollTaskScheduler
        CURRENT 1
        REVISION 0
        AGE 0
    )
endif()

install(TARGETS EpollTaskScheduler EXPORT live555Target
    INCLUDES DESTINATION include/EpollTaskScheduler
    RUNTIME DESTINATION bin/$<CONFIG>
    LIBRARY DESTINATION lib/$<CONFIG>
    ARCHIVE DESTINATION lib/$<CONFIG>
)
install(FILES ${headers}
    DESTINATION include/EpollTaskScheduler
)
