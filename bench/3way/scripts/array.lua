local total = 0
for i = 0, 99999 do
    local arr = {i, i + 1, i + 2, i + 3}
    total = total + arr[1] + arr[2] + arr[3] + arr[4]
end
