-- String concatenation benchmark: build strings x 10K

for i = 1, 10000 do
    local s = "hello" .. " " .. "world" .. " " .. "test"
end
