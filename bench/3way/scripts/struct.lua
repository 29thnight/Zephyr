local total = 0
for i = 0, 99999 do
    local v = {x = i, y = i + 1}
    total = total + v.x + v.y
end
