$shortPrompt = 'Read the file docs\copilot_scripts\wave_t_io_gc_profiler.md and execute all the instructions in it exactly as written. Start from Step 0, read the existing code carefully, then implement Part A through D in order. Build and test after Part C.'
$copilotArgs = @('-p', $shortPrompt, '--yolo', '--add-dir', 'C:\Users\lance\OneDrive\Documents\Project Zephyr', '--no-ask-user')
& 'C:\Users\lance\AppData\Local\Microsoft\WinGet\Links\copilot.exe' @copilotArgs
