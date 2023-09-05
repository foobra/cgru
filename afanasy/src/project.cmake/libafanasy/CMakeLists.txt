file(GLOB_RECURSE src RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../../libafanasy/*.cpp")
file(GLOB_RECURSE inc RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../../libafanasy/*.h")

add_library(afanasy ${src} ${inc})
if(UNIX AND NOT APPLE)
   set_target_properties(afanasy PROPERTIES COMPILE_FLAGS "-fPIC $ENV{AF_ADD_CFLAGS}")
endif(UNIX AND NOT APPLE)
target_link_libraries(afanasy ${PYTHON_LIBRARIES})


if (WIN32 AND MSVC)
   if(CMAKE_BUILD_TYPE STREQUAL "Debug")
      set(BUILD_TYPE_PATH "debug")
      set(BUILD_TYPE_PATH2 ".")
      set(rdkafka_libs lz4d rdkafka rdkafka++)
   else()
      set(BUILD_TYPE_PATH ".")
      set(BUILD_TYPE_PATH2 "RelWithDebInfo")
      set(rdkafka_libs lz4 rdkafka rdkafka++)
   endif()


   foreach(lib ${rdkafka_libs})
         target_link_libraries(
               afanasy
               ${CMAKE_CURRENT_SOURCE_DIR}/../vendors/win/rdkafka/${BUILD_TYPE_PATH}/lib/${lib}.lib
         )
   endforeach(lib ${rdkafka_libs})


   foreach(lib ${rdkafka_libs})
   add_custom_command(
      TARGET afanasy
      POST_BUILD
      COMMAND
         ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/../vendors/win/rdkafka/${BUILD_TYPE_PATH}/bin/${lib}.dll
         $<TARGET_FILE_DIR:afanasy>/../../bin/${BUILD_TYPE_PATH2}/
   )
   endforeach(lib ${rdkafka_dlls})
endif(WIN32 AND MSVC)

# add_definitions(-DCGRU_REVISION=$ENV{CGRU_REVISION})
add_definitions(-DCGRU_VERSION=$ENV{CGRU_VERSION})
