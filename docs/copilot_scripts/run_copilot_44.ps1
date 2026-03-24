$prompt = @'
Wave D Task 4.4: BytecodeInstruction Compression for Project Zephyr C++20 game scripting VM.

Completed: Wave A/B/C all done. Wave D 1.4 NaN-boxing (Value=uint64_t, sizeof==8). Wave D 1.5 Shape IC (StructInstanceObject uses Shape* + vector<Value>, mutable ic_shape/ic_slot on BytecodeInstruction).

Working directory: C:\Users\lance\OneDrive\Documents\Project Zephyr
Key files: src/zephyr_compiler.inl (BytecodeInstruction), src/zephyr_gc.inl (VM dispatch), tests/tests.cpp, docs/process.md.

TASK: Compress BytecodeInstruction from ~120+ bytes to a compact hot struct by moving cold data to a sidecar.

Step 1: Read src/zephyr_compiler.inl to understand current BytecodeInstruction fields.

Step 2: Define CompactInstruction with op (BytecodeOp), int32_t operand, uint32_t span_line, and the existing mutable ic_shape/ic_slot IC fields. Add static_assert(sizeof(CompactInstruction) <= 24).

Step 3: Define InstructionMetadata sidecar struct with std::string string_operand and std::vector<int32_t> jump_table for cold/rare data.

Step 4: Update BytecodeFunction to hold parallel vectors: instructions (CompactInstruction) and metadata (InstructionMetadata), same size.

Step 5: Update compiler emit functions to populate both vectors.

Step 6: Update VM dispatch loop so hot opcodes (arithmetic, jumps, local load/store) only touch CompactInstruction fields. Cold opcodes access metadata[ip] for string_operand etc.

Step 7: Build with MSBuild (find under C:\Program Files\Microsoft Visual Studio\). Run x64\Release\zephyr_tests.exe. Fix failures.

Step 8: Update docs/process.md to mark 4.4 Instruction Compression as complete with sizeof info.

Success: static_assert passes, all tests pass, docs/process.md updated.
'@

Set-Location 'C:\Users\lance\OneDrive\Documents\Project Zephyr'
$copilotArgs = @('-p', $prompt, '--yolo', '--add-dir', 'C:\Users\lance\OneDrive\Documents\Project Zephyr')
& 'C:\Users\lance\AppData\Local\Microsoft\WinGet\Links\copilot.exe' @copilotArgs
