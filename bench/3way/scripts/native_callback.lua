-- Simulates host API call pattern: deep function call chain (10 levels deep x 10K calls)

local function level10(x) return x + 1 end
local function level9(x) return level10(x) end
local function level8(x) return level9(x) end
local function level7(x) return level8(x) end
local function level6(x) return level7(x) end
local function level5(x) return level6(x) end
local function level4(x) return level5(x) end
local function level3(x) return level4(x) end
local function level2(x) return level3(x) end
local function level1(x) return level2(x) end

local total = 0
for i = 1, 10000 do
    total = level1(total)
end
