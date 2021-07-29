add_llvm_component_library(DPP
  SVFInitPass.cpp
  DPPAnalysis.cpp
  DPPRule1.cpp
  DPPRule2.cpp
  DPPRule3.cpp
  DPPRule5.cpp
  DPPRule6.cpp
  DPPRule7.cpp
  DPPRule8.cpp
  DPPRule9.cpp
  DPPUtils.cpp
  DPPInstr.cpp

  ADDITIONAL_HEADER_DIRS
  ${SVF_SOURCE_DIR}/include

  DEPENDS
  intrinsics_gen
  Svf

  LINK_LIBS
  Svf
)

include_directories(${SVF_SOURCE_DIR}/include)
