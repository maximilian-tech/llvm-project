set(LLVM_LINK_COMPONENTS
  Core
  IRReader
  Support
  IPO
  Passes
  )

add_llvm_tool(input-gen
  input-gen.cpp

  DEPENDS
  )
