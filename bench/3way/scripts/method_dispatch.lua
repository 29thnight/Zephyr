-- Object method call benchmark: table with method functions x 100K

local Entity = {}
Entity.__index = Entity

function Entity.new(x, y, hp)
    return setmetatable({x = x, y = y, hp = hp}, Entity)
end

function Entity:move(dx)
    return self.x + dx
end

function Entity:damage(dmg)
    return self.hp - dmg
end

function Entity:heal(amount)
    return self.hp + amount
end

local e = Entity.new(0, 0, 100)
local total = 0
for i = 0, 99999 do
    total = total + e:move(i)
    total = total + e:damage(1)
    total = total + e:heal(2)
end
