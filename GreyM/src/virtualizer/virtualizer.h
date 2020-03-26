#pragma once

#include "../../Interpreter/src/main.h"

class Shellcode;

namespace virtualizer {

bool IsVirtualizeable( const cs_insn& instruction, const VmOpcodes vm_opcode );
VmOpcodes GetVmOpcode( const cs_insn& instruction );

Shellcode CreateVirtualizedShellcode(
    const cs_insn& instruction,
    const VmOpcodes vm_opcode,
    const uint32_t vm_opcode_encyption_key,
    const uint32_t relocation_count,
    const std::array<uint32_t, 16>& relocations );

Shellcode GetLoaderShellcodeForVirtualizedCode( const cs_insn& instruction,
                                                const VmOpcodes vm_opcode,
                                                const uintptr_t image_base );

}  // namespace virtualizer