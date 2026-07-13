-- Assertion helpers for Lua Test Runner
-- These are automatically loaded by runner.lua

sgs.assert = {}

function sgs.assert.equal(actual, expected, msg, runner)
    local passed = (actual == expected)
    local fullMsg = msg or string.format("expected %s but got %s", tostring(expected), tostring(actual))
    if runner then
        runner:addResult(passed, fullMsg)
    else
        print((passed and "[PASS]" or "[FAIL]") .. " " .. fullMsg)
    end
    return passed
end

function sgs.assert.true(value, msg, runner)
    return sgs.assert.equal(value, true, msg, runner)
end

function sgs.assert.notNil(value, msg, runner)
    return sgs.assert.equal(value ~= nil, true, msg or "value should not be nil", runner)
end

function sgs.assert.greater(actual, expected, msg, runner)
    local passed = actual > expected
    local fullMsg = msg or string.format("expected > %d but got %d", expected, actual)
    if runner then
        runner:addResult(passed, fullMsg)
    else
        print((passed and "[PASS]" or "[FAIL]") .. " " .. fullMsg)
    end
    return passed
end

function sgs.assert.less(actual, expected, msg, runner)
    local passed = actual < expected
    local fullMsg = msg or string.format("expected < %d but got %d", expected, actual)
    if runner then
        runner:addResult(passed, fullMsg)
    else
        print((passed and "[PASS]" or "[FAIL]") .. " " .. fullMsg)
    end
    return passed
end

function sgs.assert.between(actual, min, max, msg, runner)
    local passed = actual >= min and actual <= max
    local fullMsg = msg or string.format("expected between [%d,%d] but got %d", min, max, actual)
    if runner then
        runner:addResult(passed, fullMsg)
    else
        print((passed and "[PASS]" or "[FAIL]") .. " " .. fullMsg)
    end
    return passed
end

function sgs.assert.contains(list, item, msg, runner)
    local passed = false
    for _, v in ipairs(list) do
        if v == item then
            passed = true
            break
        end
    end
    local fullMsg = msg or string.format("list should contain %s", tostring(item))
    if runner then
        runner:addResult(passed, fullMsg)
    else
        print((passed and "[PASS]" or "[FAIL]") .. " " .. fullMsg)
    end
    return passed
end
