local total = 0
for i = 0, 999999 do
    local ax, ay = i % 1000, (i + 1) % 1000
    local bx, by = (i + 2) % 1000, (i + 3) % 1000
    local dot = ax * bx + ay * by
    local dx, dy = ax - bx, ay - by
    local dist_sq = dx * dx + dy * dy
    total = (total + dot + dist_sq) % 1000000
end
