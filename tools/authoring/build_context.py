#!/usr/bin/env python3
"""Refresh the bounded authoring contract from this checkout; never execute Lua."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / 'src/dialog/authoring/context.json'

def build():
    swig = (ROOT / 'swig/sanguosha.i').read_text(encoding='utf-8-sig')
    variant = (ROOT / 'swig/qvariant.i').read_text(encoding='utf-8-sig')
    helpers = (ROOT / 'lua/sgs_ex.lua').read_text(encoding='utf-8-sig')
    methods = ['objectName', 'getHp', 'getMaxHp', 'getLostHp', 'isWounded', 'getPhase',
               'isAlive', 'isDead', 'hasSkill', 'getMark', 'getRole', 'drawCards',
               'askForSkillInvoke', 'askForChoice', 'sendCompulsoryTriggerLog',
               'broadcastSkillInvoke', 'setPlayerMark', 'loseHp', 'getAlivePlayers',
               'length', 'toDamage', 'toInt', 'toBool', 'setValue']
    constants = ['EventPhaseStart', 'EventPhaseEnd', 'GameStart', 'TurnStart',
                 'DamageCaused', 'DamageInflicted', 'Damaged', 'Damage',
                 'Skill_NotFrequent', 'Skill_Frequent', 'Skill_Compulsory',
                 'Player_Start', 'Player_Draw', 'Player_Play', 'Player_Finish', 'Player_NotActive']
    signatures = []
    for method in methods:
        lines = [line.strip() for line in (swig + '\n' + variant + '\n' + (ROOT / 'swig/list.i').read_text()).splitlines()
                 if method + '(' in line or method + ' (' in line]
        if method == 'objectName':
            lines = ['QObject::objectName(): QString (swig/sanguosha.i:106)']
            assert 'objectName()' in swig
        if method == 'length':
            lines = ['QList::length(): int (swig/list.i)']
            assert 'length()' in (ROOT / 'swig/list.i').read_text()
        assert lines, method
        signatures.extend(lines[:3])
    for value in constants:
        symbol = value.removeprefix('Skill_').removeprefix('Player_')
        assert symbol in swig, value
    helper = helpers[helpers.index('function sgs.CreateTriggerSkillV2(spec)'):helpers.index('\n-- Rules supply', helpers.index('function sgs.CreateTriggerSkillV2(spec)'))]
    # Actual callback and context example, kept small and pinned with its digest.
    example_path = ROOT / 'extensions/AIgeneral.lua'
    example = example_path.read_text(encoding='utf-8-sig').splitlines()
    snippets = {
        'lua/sgs_ex.lua:80 (CreateTriggerSkillV2)': helper,
        'swig/sanguosha.i + swig/qvariant.i (selected exposed signatures)': '\n'.join(signatures),
        'swig/sanguosha.i:159 (General constructor)': 'General(Package*, const char* name, const char* kingdom, int max_hp=4, bool male=true, bool hidden=false, bool never_shown=false, int start_hp=INT_MAX, int start_hujia=0);',
        'extensions/AIgeneral.lua:14-32 (actual V2 can_trigger and on_cost callbacks)': '\n'.join(example[13:32]),
        'extensions/AIgeneral.lua:48-52 (on_effect excerpt; ctx.original_data used above)': '\n'.join(example[47:52]),
    }
    sources = ['swig/sanguosha.i', 'swig/qvariant.i', 'swig/list.i', 'swig/native.i', 'lua/sgs_ex.lua', 'extensions/AIgeneral.lua']
    result = {'contract_version': 1, 'baseline': '548b5c45',
              'scope': 'MVP: local CreateTriggerSkillV2 definitions only. No generic upstream APIs; no convenience helpers from feature/lua-state-convenience. Unsupported skills require manual authoring or a future verified contract expansion.',
              'globals': ['CreateTriggerSkillV2'] + constants, 'methods': methods,
              'lua_functions': ['pairs', 'ipairs', 'tonumber', 'tostring', 'type', 'assert', 'error', 'select', 'next'],
              'library_functions': {'math': ['abs', 'ceil', 'floor', 'max', 'min'], 'string': ['format', 'sub', 'find', 'gsub', 'match', 'lower', 'upper', 'len'], 'table': ['concat', 'insert', 'remove', 'sort']},
              'sources': {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in sources},
              'references': snippets,
              'callback_contract': 'can_trigger(skill,event,room,player,data) returns skill:objectName() or false. on_cost(skill,event,room,player,ctx) returns boolean; on_effect receives ctx.original_data, not legacy data. Event actor and payload depend on event; guard nil player and check player:hasSkill(skill:objectName()) for owned trigger skills. Start-of-turn draw skills can use EventPhaseStart and Player_Start. Do not invent local helper call names; declare any local functions explicitly. Never fabricate methods, contexts or enum names. Return notes for unsupported behavior. Use only globals/methods in this contract; helpers implementation internals are reference, not additional permitted APIs.'}
    assert len(json.dumps(result, ensure_ascii=False).encode()) < 24000
    return result

if __name__ == '__main__':
    OUTPUT.write_text(json.dumps(build(), ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
