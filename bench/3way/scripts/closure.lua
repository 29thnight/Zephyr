local function make_adder(base)
    return function(value)
        return value + base
    end
end

local total = 0
for i = 0, 99999 do
    local add_i = make_adder(i)
    total = total + add_i(1)
end
