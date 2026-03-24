Set-Location 'C:\Users\lance\OneDrive\Documents\Project Zephyr'

$copilotArgs = @(
    '-p', 'Read docs/copilot_scripts/wave_f_instructions.md and follow all the steps in it exactly.',
    '--yolo',
    '--add-dir', 'C:\Users\lance\OneDrive\Documents\Project Zephyr'
)

& 'C:\Users\lance\AppData\Local\Microsoft\WinGet\Links\copilot.exe' @copilotArgs
