$shortPrompt = 'Read the file docs\copilot_scripts\wave_j2_coroutine_optimize.md and execute all the instructions in it exactly as written. Start from Step 0 and work through each step sequentially.'
$copilotArgs = @('-p', $shortPrompt, '--yolo', '--add-dir', 'C:\Users\lance\OneDrive\Documents\Project Zephyr', '--no-ask-user')
& 'C:\Users\lance\AppData\Local\Microsoft\WinGet\Links\copilot.exe' @copilotArgs
