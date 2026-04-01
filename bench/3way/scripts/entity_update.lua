local px, py = 0, 0
local vx, vy = 1, 0
local gravity = -1

for frame = 1, 100 do
    for entity = 1, 10000 do
        px = px + vx
        py = py + vy
        vy = vy + gravity
    end
end
