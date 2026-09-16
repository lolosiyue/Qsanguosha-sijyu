-- This is the start script of QSanguosha

-- Takeover state is opt-in and explicit: extensions register a versioned
-- export/restore pair; the C++ runtime never serializes the Lua VM itself.
do
	local registerTakeoverStateProvider = rawget(_G, "__qsan_register_takeover_state_provider")
	if registerTakeoverStateProvider then
		function sgs.RegisterTakeoverStateProvider(name, version, export, restore)
			return registerTakeoverStateProvider(name, version, export, restore)
		end
	end
end

package.path = package.path .. ";./lua/lib/?.lua"

dofile "lua/utilities.lua"
dofile "lua/sgs_ex.lua"

-- Package modules keep a qualified require cache. Unqualified imports first
-- consult this package's declared modules, then retain the legacy search path.
local function package_module_name(path)
	return path:gsub("%.lua$", ""):gsub("/", ".")
end
local original_require = require
local function package_chunk(path)
	local id = path:match("^packages/([^/]+)/")
	if not id then return assert(loadfile(path)) end
	local env = setmetatable({}, {__index = _G, __newindex = _G})
	rawset(env, "require", function(name)
		local qualified = "packages." .. id .. ".lua." .. name
		if package.preload[qualified] then return original_require(qualified) end
		return original_require(name)
	end)
	return assert(loadfile(path, "t", env))
end
for _, path in ipairs(sgs.GetConfigList("package_lua")) do
	-- The client rules filesystem omits server-only AI. Register modules lazily
	-- so their payload is required only in the runtime that actually uses them.
	package.preload[package_module_name(path)] = function(...)
		return package_chunk(path)(...)
	end
end
function sgs.RequirePackage(id, name)
	assert(id:match("^[a-z0-9][a-z0-9_-]*$") and name:match("^[%w_.-]+$"), "invalid package module")
	return require("packages." .. id .. ".lua." .. name)
end
function sgs.LoadPackageScript(path)
	return package_chunk(path)()
end

local package_names = {}
local extension_ids = sgs.GetConfigList("extension_ids")
for entry_index, script in ipairs(sgs.GetConfigList("extension_names")) do
	local legacy_name = script:match("^extensions/(.+)%.lua$")
	local module_name = legacy_name and ("extensions." .. legacy_name) or package_module_name(script)
	-- Load the declared path exactly: require's dotted-name search would turn
	-- a valid filename such as probe.one.lua into probe/one.lua.
	local chunk = package_chunk(script)
	local old_module = "extensions." .. (extension_ids[entry_index] or legacy_name or module_name)
	if not legacy_name then
		package.preload[old_module] = function() return require(module_name) end
	end
	package.preload[module_name] = function(...)
		local result = chunk(...)
		-- Lua 5.1-style module() exports to its historic extension name.
		if result == nil and not legacy_name then return package.loaded[old_module] end
		return result
	end
	local loaded = require(module_name)
	if sgs.GetConfig("DisableLua", false) then continue end
	if type(loaded) == "table" and loaded.hidden ~= true then -- need to consider the compatibility of 'module'
		if #loaded > 0 then
			for _, extension in ipairs(loaded) do
				if extension:inherits("Package") then
					table.insert(package_names, extension:objectName())
					sgs.Sanguosha:addPackage(extension)
				end
			end
		else
			table.insert(package_names, loaded.extension:objectName())
			sgs.Sanguosha:addPackage(loaded.extension)
		end
	elseif type(loaded) == "userdata" and loaded:inherits("Package") then
		table.insert(package_names, loaded:objectName())
		sgs.Sanguosha:addPackage(loaded)
	end
end
if not sgs.Sanguosha:isGameLuaRuntime() then
	sgs.SetConfig("LuaPackages", table.concat(package_names, "+"))
end

local skillList = sgs.SkillList()
if not sgs.Sanguosha:getSkill("#bossModeExperience") then
	local function bossModeExpMult(level)
		return math.floor(math.log(level) / math.log(2)) + 1
	end
	local BossModeExperience = sgs.CreateTriggerSkill {
		name = "#bossModeExperience",
		events = {sgs.PreCardUsed, sgs.CardResponded, sgs.CardsMoveOneTime,
		sgs.DamageDone, sgs.HpLost, sgs.GameOverJudge},
		global = true,
		priority = 15,
		can_trigger = function(self, target)
			return target and target:getRoom():getMode() == "04_boss"
			and sgs.GetConfig("BossModeExp", false)
		end,
		on_trigger = function(self, triggerEvent, player, data)
			local room = player:getRoom()
			local level = room:getTag("BossModeLevel"):toInt() + 1
			local x = bossModeExpMult(level)
			if triggerEvent == sgs.PreCardUsed or triggerEvent == sgs.CardResponded then
				if player:isLord() then return false end
				local card
				if triggerEvent == sgs.PreCardUsed then
					card = data:toCardUse().card
				else
					card = data:toCardResponse().m_card
				end
				local typeid = card:getTypeId()
				if typeid == sgs.Card_TypeBasic then
					room:addPlayerMark(player, "@bossExp", x)
				elseif typeid == sgs.Card_TypeTrick then
					room:addPlayerMark(player, "@bossExp", 3 * x)
				elseif typeid == sgs.Card_TypeEquip then
					room:addPlayerMark(player, "@bossExp", 2 * x)
				end
			elseif triggerEvent == sgs.CardsMoveOneTime then
				if player:isLord() then return false end
				local move = data:toMoveOneTime()
				if not move.to or player:objectName() ~= move.to:objectName()
					or (move.from and move.from:objectName() == move.to:objectName())
					or (move.to_place ~= sgs.Player_PlaceHand and move.to_place ~= sgs.Player_PlaceEquip)
					or room:getTag("FirstRound"):toBool() then
					return false
				end
				room:addPlayerMark(player, "@bossExp", move.card_ids:length() * x)
			elseif triggerEvent == sgs.DamageDone then
				local damage = data:toDamage()
				if damage.from and not damage.from:isLord() then
					room:addPlayerMark(damage.from, "@bossExp", damage.damage * 5 * x)
				end
				if not damage.to:isLord() then
					room:addPlayerMark(damage.to, "@bossExp", damage.damage * 2 * x)
				end
			elseif triggerEvent == sgs.HpLost then
				if player:isLord() then return false end
				local lose = data:toHpLost().lose
				room:addPlayerMark(player, "@bossExp", lose * x)
			elseif triggerEvent == sgs.GameOverJudge then
				local death = data:toDeath()
				if not death.who:isLord() then
					room:removePlayerMark(death.who, "@bossExp", 100)
				else
					for _, p in sgs.qlist(room:getOtherPlayers(death.who)) do
						room:addPlayerMark(p, "@bossExp", 10 * x)
					end
					local damage = death.damage
					if damage and damage.from and damage.from:isAlive() and not damage.from:isLord() then
						room:addPlayerMark(damage.from, "@bossExp", 5 * x)
					end
				end
			end
			return false
		end
	}
	skillList:append(BossModeExperience)
end
if not sgs.Sanguosha:getSkill("#bossMaxCards") then
	local BossModeMaxCards = sgs.CreateMaxCardsSkill {
		name = "#bossMaxCards",
		fixed_func = function(self, target)
			if target:isLord() and target:getMark("BossMode_Boss") == 1
			and target:getGeneralName():startsWith("sujiang")
			then return 20 end
			return -1
		end
	}
	if BossModeMaxCards then
		skillList:append(BossModeMaxCards)
	end
end
-- Filter out nil skills before adding
local filteredList = sgs.SkillList()
for _, skill in sgs.qlist(skillList) do
	if skill then
		filteredList:append(skill)
	end
end
sgs.Sanguosha:addSkills(filteredList)

if not sgs.Sanguosha:isLuaDefinitionsLoaded() then
	sgs.Sanguosha:finishLuaDefinitions()
	function load_translation(file)
		local t = dofile(file)
		if type(t) ~= "table" then
			error(("file %s is should return a table!"):format(file))
		end
		sgs.LoadTranslationTable(t)
	end
	local lang = sgs.GetConfig("Language", "zh_CN")
	for _, dir in ipairs({"", "Audio", "Package"}) do
		local lang_dir = "lang/" .. lang .. "/" .. dir
		for _, file in ipairs(sgs.GetFileNames(lang_dir)) do
			load_translation(("%s/%s"):format(lang_dir, file))
		end
	end
	for _, file in ipairs(sgs.GetConfigList("package_lang")) do
		-- Translation remains presentation content and does not change rules IDs.
		local locale = file:match("/translation/([a-z][a-z]_[A-Z][A-Z])/")
		if locale and locale ~= lang then continue end
		local values = sgs.LoadPackageScript(file)
		assert(type(values) == "table", "package translation must return a table: " .. file)
		sgs.LoadTranslationTable(values)
	end
end
