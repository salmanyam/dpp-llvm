add_llvm_component_library(DPP
  DPPRule5.cpp
  DPPAnalysis.cpp
  DPPRule5.cpp
  DPPRule7.cpp
  DPPWhiteList.cpp

  ADDITIONAL_HEADER_DIRS
  ${SVF_SOURCE_DIR}/include

  DEPENDS
  intrinsics_gen
  Svf

  LINK_LIBS
  Svf
)

include_directories(${SVF_SOURCE_DIR}/include)
