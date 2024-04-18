set(LLVM_LINK_COMPONENTS
  Core
  BitWriter
  IRReader
  Support
  IPO
  Passes
  Analysis
  TransformUtils
  TargetParser
  )

add_llvm_tool(input-gen
  input-gen.cpp

  DEPENDS
  )
