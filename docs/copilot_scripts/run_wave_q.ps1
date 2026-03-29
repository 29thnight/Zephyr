$shortPrompt = 'Read the file docs\copilot_scripts\wave_q_stdlib.md and execute all the instructions in it exactly as written. Start from Step 0, read the APIs carefully, then do Part A (std/json) first, then Part B (std/collections). Build and test after each part.'
$copilotArgs = @('-p', $shortPrompt, '--yolo', '--add-dir', 'C:\Users\lance\OneDrive\Documents\Project Zephyr', '--no-ask-user')
& 'C:\Users\lance\AppData\Local\Microsoft\WinGet\Links\copilot.exe' @copilotArgs
