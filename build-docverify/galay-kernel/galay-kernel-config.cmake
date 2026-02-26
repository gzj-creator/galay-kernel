
####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was galay-kernel-config.cmake.in                            ########

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

set(GALAY_KERNEL_VERSION "1.0.0")
set(GALAY_KERNEL_BACKEND "kqueue")

# 设置包含目录
set_and_check(GALAY_KERNEL_INCLUDE_DIR "${PACKAGE_PREFIX_DIR}/include/galay-kernel")

# 提供导入的目标
include("${CMAKE_CURRENT_LIST_DIR}/galay-kernel-targets.cmake")

# 设置别名以便于使用
if(NOT TARGET galay-kernel::galay-kernel)
    add_library(galay-kernel::galay-kernel ALIAS galay-kernel)
endif()

# 检查组件
check_required_components(galay-kernel)
