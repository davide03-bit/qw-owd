# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

#
# This module sets the following variables:
#   mvfst_FOUND
#   mvfst_INCLUDE_DIRS
#
# This module exports the following target:
#    mvfst::mvfst
#
# which can be used with target_link_libraries() to pull in the mvfst
# library.


####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was mvfst-config.cmake.in                            ########

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

include(CMakeFindDependencyMacro)
find_dependency(folly)
find_dependency(Fizz)
find_dependency(Threads)
find_dependency(Boost COMPONENTS iostreams system thread filesystem regex context)

if(NOT TARGET mvfst::mvfst_transport)
  include("${CMAKE_CURRENT_LIST_DIR}/mvfst-targets.cmake")
  get_target_property(mvfst_INCLUDE_DIRS mvfst::mvfst_transport INTERFACE_INCLUDE_DIRECTORIES)
endif()

set(mvfst_LIBRARIES
  mvfst::mvfst_constants
  mvfst::mvfst_exception
  mvfst::mvfst_transport
  mvfst::mvfst_client
  mvfst::mvfst_codec_types
  mvfst::mvfst_codec_decode
  mvfst::mvfst_codec_pktbuilder
  mvfst::mvfst_codec_pktrebuilder
  mvfst::mvfst_codec_packet_number_cipher
  mvfst::mvfst_codec
  mvfst::mvfst_looper
  mvfst::mvfst_buf_accessor
  mvfst::mvfst_bufutil
  mvfst::mvfst_transport_knobs
  mvfst::mvfst_cc_algo
  mvfst::mvfst_dsr_types
  mvfst::mvfst_dsr_frontend
  mvfst::mvfst_fizz_client
  mvfst::mvfst_fizz_handshake
  mvfst::mvfst_flowcontrol
  mvfst::mvfst_handshake
  mvfst::mvfst_happyeyeballs
  mvfst::mvfst_qlogger
  mvfst::mvfst_loss
  mvfst::mvfst_observer
  mvfst::mvfst_server
  mvfst::mvfst_server_async_tran
  mvfst::mvfst_server_state
  mvfst::mvfst_state_machine
  mvfst::mvfst_state_ack_handler
  mvfst::mvfst_state_datagram_handler
  mvfst::mvfst_state_stream_functions
  mvfst::mvfst_state_pacing_functions
  mvfst::mvfst_state_functions
  mvfst::mvfst_state_simple_frame_functions
  mvfst::mvfst_state_stream
)

if(NOT mvfst_FIND_QUIETLY)
  message(STATUS "Found mvfst: ${PACKAGE_PREFIX_DIR}")
endif()
