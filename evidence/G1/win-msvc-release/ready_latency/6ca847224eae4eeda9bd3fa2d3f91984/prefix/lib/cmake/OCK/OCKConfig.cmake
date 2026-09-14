
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
set(OCK_SDK_VERSION "0.1.0-dev.8")
set(OCK_IMPLEMENTATION_STAGE "B6Subset")
set(OCK_RUNTIME_AVAILABLE TRUE)
set(OCK_INSTALLATION_PROFILE "Runtime")
if(TARGET OCK::Control)
  set(OCK_SCHEMA_DIR "${PACKAGE_PREFIX_DIR}/share/ock/schemas")
endif()
# B6 开发 SDK 可消费已接线组件；不替代运行时能力与包级验收。
foreach(component IN LISTS OCK_FIND_COMPONENTS)
  if((component STREQUAL "Foundation" OR component STREQUAL "CoreContracts" OR component STREQUAL "Runtime" OR component STREQUAL "State" OR component STREQUAL "Data" OR component STREQUAL "Dynamic" OR component STREQUAL "ControlProtocol" OR component STREQUAL "Control" OR component STREQUAL "ControlClient" OR component STREQUAL "Adapter::LocalIPC") AND TARGET OCK::${component})
    set(OCK_${component}_FOUND TRUE)
  elseif(component STREQUAL "Adapter::CpuPool" AND "OFF" STREQUAL "ON" AND TARGET OCK::Adapter::CpuPool)
    set(OCK_${component}_FOUND TRUE)
  else()
    set(OCK_${component}_FOUND FALSE)
    string(APPEND OCK_NOT_FOUND_MESSAGE "OCK component ${component} is not implemented in the current SDK. ")
  endif()
endforeach()
check_required_components(OCK)
