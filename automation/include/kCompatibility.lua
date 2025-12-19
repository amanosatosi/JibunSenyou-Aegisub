-- compatibility.lua
-- Put this in automation/include/ so KFX templates / karaoke templates can require or auto-load this

local compat = {}

-- If unicode module exists
local unicode_mod = rawget(_G, "unicode")
if not unicode_mod then
    -- Try requiring
    local ok, u = pcall(require, "unicode")
    if ok then unicode_mod = u end
end

if unicode_mod and unicode_mod.len then
    -- Store old
    local old_unicode_len = unicode_mod.len

    -- Define new len that discards extra values
    unicode_mod.len = function(str)
        -- ensure str is a string
        if type(str) ~= "string" then
            -- optionally coerce
            str = tostring(str)
        end
        -- call old, but discard extra returns
        local result = old_unicode_len(str)
        return result
    end

    compat.unicode_len = unicode_mod.len
end

-- If unicode module doesn't exist, define minimal unicode.len
if not (unicode_mod and unicode_mod.len) then
    -- define minimal len: count bytes or count UTF-8 codepoints
    unicode_mod = unicode_mod or {}
    unicode_mod.len = function(str)
        if type(str) ~= "string" then str = tostring(str) end
        -- simple fallback: count bytes
        -- Better fallback: count UTF-8 codepoints
        local _, count = str:gsub("[\128-\191]", "")
        -- Explanation: In UTF-8, continuation bytes are 10xxxxxx (128–191), so a codepoint
        -- is a byte that is *not* a continuation byte.
        -- This method counts number of initial bytes in codepoints.
        -- It overestimates somewhat for malformed strings, but works for many cases.
        return count
    end
    compat.unicode_len = unicode_mod.len
    -- Export unicode_mod into _G if needed
    _G.unicode = unicode_mod
end

return compat
