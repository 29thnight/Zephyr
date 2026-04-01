local function handle_move(x) return x + 1 end
local function handle_attack(x) return x + 10 end
local function handle_heal(x) return x + 5 end
local function handle_idle(x) return x end

local total = 0
for i = 0, 99999 do
    local event_type = i % 4
    if event_type == 0 then
        total = handle_move(total)
    elseif event_type == 1 then
        total = handle_attack(total)
    elseif event_type == 2 then
        total = handle_heal(total)
    else
        total = handle_idle(total)
    end
end
