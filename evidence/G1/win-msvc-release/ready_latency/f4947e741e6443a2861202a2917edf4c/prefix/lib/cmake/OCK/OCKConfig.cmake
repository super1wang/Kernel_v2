
####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was OCKConfig.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################
include("${CMAKE_CURRENT_LIST_DIR}/OCKThirdPartyTargets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/OCKTargets.cmake")
set(OCK_SDK_VERSION "0.1.0-dev.2")
set(OCK_IMPLEMENTATION_STAGE "NativeSubset")
set(OCK_RUNTIME_AVAILABLE TRUE)
# D1.06仅提供NativeSubset；其他尚未实施组件明确拒绝。
foreach(component IN LISTS OCK_FIND_COMPONENTS)
  if(component STREQUAL "Foundation" OR component STREQUAL "CoreContracts" OR component STREQUAL "Runtime")
    set(OCK_${component}_FOUND TRUE)
  else()
    set(OCK_${component}_FOUND FALSE)
    string(APPEND OCK_NOT_FOUND_MESSAGE "OCK component ${component} is not implemented in the current SDK. ")
  endif()
endforeach()
check_required_components(OCK)
