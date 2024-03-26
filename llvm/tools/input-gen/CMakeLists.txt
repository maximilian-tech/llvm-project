set(LLVM_LINK_COMPONENTS
  Core
  IRReader
  Support
  )

add_llvm_tool(input-gen
  input-gen.cpp

  DEPENDS
  )
