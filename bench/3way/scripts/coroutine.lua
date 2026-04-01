local function counter(limit)
    return coroutine.wrap(function()
        for i = 0, limit - 1 do
            coroutine.yield(i)
        end
        return limit
    end)
end

local total = 0
for round = 1, 100 do
    local co = counter(1000)
    local val = co()
    while val < 1000 do
        total = total + val
        val = co()
    end
end
