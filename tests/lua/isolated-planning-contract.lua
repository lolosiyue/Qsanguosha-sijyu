-- PR07: immutable authority rows, branch isolation, context and bounded planning.
local function player(name)
    return {object_name=name, alive=true, dead=false, hp=4, max_hp=4,
        handcard_count=0, hujia=0, max_cards=4, attack_range=1, hand_visible=true, equips={}, judging_area={}, skills={}, public_marks={}, known_cards={}}
end
local function request()
    return {viewer='v', kind='activate', reason=1, pattern='', handling_method=1,
        scratch={marker={value=7}}, conversions_enumerated=true, card_conversions={},
        world_view={self=player('v'), players={player('a'),player('b'),player('c')},
            hand_cards={}, player_order={'v','a','b','c'}, alive_player_order={'v','a','b','c'}, mode_policy={managed=true, objectives={},
                relations={v={a='enemy',b='enemy',c='enemy'}}}},
        card_candidates={{card_id=1, candidate_id=1, available=true, limited=false,
            complete_coverage=true, target_fixed=false, feasible_with_no_target=false,
            legal_targets={'a','b'}, target_combinations={{'a','b'},{'b','c'}}},
            -- Nested planning must project every physical card it can propose;
            -- otherwise the final-ticket check rejects the child or outer plan.
            {card_id=10, candidate_id=10, available=true, limited=false,
                complete_coverage=true, target_fixed=true, feasible_with_no_target=true,
                legal_targets={}, target_combinations={{}}},
            {card_id=11, candidate_id=11, available=true, limited=false,
                complete_coverage=true, target_fixed=true, feasible_with_no_target=true,
                legal_targets={}, target_combinations={{}}}}
    }
end
local function card(id, name)
    return CardView.new({id=id, effective_id=id, name=name, class_name=name,
        kind_of={name,'Card'}, suit=0, number=1})
end
local r=request()
local ai=assert(SmartAIView.new(r))
local candidate=ai:getCardCandidate(1)
local nexts, finish=candidate:getTargetSelection({})
assert(#nexts==2 and not finish, 'root selection lost')
nexts,finish=candidate:getTargetSelection({'a'})
assert(#nexts==1 and nexts[1]=='b' and not finish, 'prefix constraint lost')
nexts,finish=candidate:getTargetSelection({'a','b'})
assert(#nexts==0 and finish, 'completion lost')
nexts,finish=candidate:getTargetSelection({'b','a'})
assert(#nexts==0 and not finish, 'order lost')
local page,cursor=candidate:getTargetCombinations(0,1)
assert(#page==1 and cursor==1)
page[1][1]='pollution'
assert(candidate:getTargetCombinations(0,1)[1][1]=='a', 'page aliases authority')
local slash=card(1,'Slash')
local plan,status=ai:tryUseCard(slash)
assert(status=='planned' and #plan.to==2, 'multi-target Slash truncated: '..tostring(status)..' / '..tostring(plan.reason))
assert(plan.to[1]:objectName()=='a' and plan.to[2]:objectName()=='b', 'illegal combination')
assert(r.scratch.marker.value==7 and r.scratch.reserved_cards==nil, 'root polluted')
local missing=request(); missing.world_view.player_order=nil
assert(select(2,SmartAIView.new(missing):tryUseCard(slash))=='unsupported', 'missing roster became no targets')
r.card_candidates[1].complete_coverage=false
local _,status=ai:tryUseCard(slash)
assert(status=='unsupported', 'incomplete targets became declined')
r.card_candidates[1].complete_coverage=true
r.card_candidates[1].target_combinations=nil
local _,status=ai:tryUseCard(slash)
assert(status=='unsupported', 'unknown targets became empty')

-- Static-review additions: unknown projections/relations must not become declined.
local unknown=request(); unknown.card_candidates[1].legal_targets=nil
assert(select(2,SmartAIView.new(unknown):tryUseCard(slash))=='unsupported')
unknown=request(); unknown.card_candidates[1].complete_coverage=false
unknown.card_candidates[1].legal_targets={}; unknown.card_candidates[1].target_combinations={}
assert(select(2,SmartAIView.new(unknown):tryUseCard(slash))=='unsupported')
unknown=request(); unknown.world_view.mode_policy.relations.v.a='unknown'
local unknown_ai=SmartAIView.new(unknown)
assert(select(2,unknown_ai:tryUseCard(slash))=='unsupported')
assert(unknown_ai:pickTargets({targets={'a'}})==nil, 'unknown ranking became empty')
local empty=request(); empty.card_candidates[1].legal_targets={}
empty.card_candidates[1].target_combinations={}
local empty_ai=SmartAIView.new(empty)
assert(#empty_ai:getCardCandidate(1):getTargetCombinations()==0)
assert(select(2,empty_ai:tryUseCard(slash))=='declined', 'known empty targets became unknown')
-- Generic planning must reject missing projection data before it can skip a card.
local function turn_request()
    local turn=request()
    -- The turn-use fixture offers only its projected hand card; nested-only
    -- probes below have separate tickets and are not alternatives in this turn.
    turn.card_candidates={turn.card_candidates[1]}
    turn.world_view.hand_cards={{id=1,effective_id=1,name='slash',class_name='Slash',
        kind_of={'Slash','BasicCard','Card'},suit=0,number=1}}
    return turn
end
local unknown_turn=turn_request(); unknown_turn.card_candidates[1].target_combinations=nil
local covered=AIUnsupported.capture(function() return SmartAIView.new(unknown_turn):getTurnUse() end)
assert(not covered, 'generic planner skipped unknown combinations')
local empty_turn=turn_request(); empty_turn.card_candidates[1].target_combinations={}
assert(#SmartAIView.new(empty_turn):getTurnUse()==0, 'generic planner offered an infeasible sequence')
-- Candidate availability is authoritative per card: known false is skipped,
-- while an unknown availability remains an uncovered projection.
local blocked_turn=turn_request(); blocked_turn.card_candidates[1].available=false
assert(#SmartAIView.new(blocked_turn):getTurnUse()==0, 'unavailable candidate was offered')
local unknown_availability=turn_request(); unknown_availability.card_candidates[1].available=nil
assert(not AIUnsupported.capture(function()
    return SmartAIView.new(unknown_availability):getTurnUse()
end), 'unknown availability was silently skipped')

local outer,inner=card(10,'PR07Outer'),card(11,'PR07Inner')
ai_card_use.PR07Inner=function(self,c,use,context)
    assert(self.request.scratch.marker.value==9)
    assert(self.request.scratch.reserved_cards[10] and self.request.scratch.reserved_cards[11])
    assert(context.kind=='use_card' and context.reason==18 and context.pattern=='slash')
    self.request.scratch.marker.value=100
    use.card=c
end
ai_card_use.PR07Outer=function(self,c,use,context)
    assert(context.reason==18 and use.context.reason==18)
    self.request.scratch.marker.value=9
    local child=self:aiUseCard(inner)
    assert(child.scratch.marker.value==100)
    assert(self.request.scratch.marker.value==9 and not self.request.scratch.reserved_cards[11], 'child polluted parent')
    local _,blocked=self:tryUseCard(c)
    assert(blocked=='unsupported', 'reserved cost reused')
    use.card=c
end
local nested=request(); nested.kind='use_card'; nested.reason=18; nested.pattern='slash'
local na=assert(SmartAIView.new(nested))
local p=na:aiUseCard(outer)
assert(p.scratch.marker.value==9 and nested.scratch.marker.value==7)
local q=na:aiUseCard(outer)
assert(q.scratch~=p.scratch and q.scratch.marker~=p.scratch.marker, 'sibling alias')
p.scratch.marker.value=88
assert(q.scratch.marker.value==9)
-- Standard Slash has no response strategy: no accidental play-stage decision.
local _,st=na:tryUseCard(slash)
assert(st=='unsupported', 'response applied play strategy')
local response=request(); response.kind='respond_card'; response.reason=2
assert(select(2, SmartAIView.new(response):tryUseCard(slash))=='unsupported')
-- Ordinary failures still unwind and stay errors.
ai_card_use.PR07Outer=function(self) self.request.scratch.marker.value=33; error('PR07 expected error') end
local ok,err=pcall(function() na:tryUseCard(outer) end)
assert(not ok and tostring(err):find('PR07 expected error',1,true))
assert(nested.scratch.marker.value==7, 'error unwind leaked branch')

local loop=ConversionView.new({conversion_id=7,name='PR07Loop',class_name='PR07Loop',subcards={}})
local loop_calls=0
ai_card_use.PR07Loop=function(self,c,use) loop_calls=loop_calls+1; use.card=self:aiUseCard(c).card end
local _,limit=SmartAIView.new(request()):tryUseCard(loop)
assert(limit=='unsupported' and loop_calls==8, 'recursion limit lost')
ai_card_use.PR07Inner=function(_,c,use) use.card=c end
local budget=SmartAIView.new(request())
for i=1,128 do assert(select(2,budget:tryUseCard(inner))=='planned') end
local signal,limited=budget:tryUseCard(inner)
assert(limited=='unsupported' and signal.key=='planning', 'candidate budget lost')
budget.request.scratch={}
assert(select(2,budget:tryUseCard(inner))=='unsupported', 'scratch replacement reset budget')
assert(select(2,SmartAIView.new(budget.request):tryUseCard(inner))=='unsupported', 'facade replacement reset budget')
-- Dispatch records budget exhaustion as uncovered, never pass.
ai_register_handler('pr07_budget',function(self) return self:aiUseCard(loop):toAnswer() end)
local exhausted=request(); exhausted.kind='pr07_budget'
ai_coverage.clearUncovered()
assert(ai_decide(exhausted)==nil)
local uncovered=ai_coverage.uncovered()
assert(#uncovered==1 and uncovered[1].key=='planning')
-- Reused input does not reuse a prior decision's scratch.
ai_register_handler('pr07_reset',function(self,req) assert(next(req.scratch)==nil); req.scratch.old=true; return {kind='pass'} end)
local reused=request(); reused.kind='pr07_reset'
assert(ai_decide(reused).kind=='pass' and ai_decide(reused).kind=='pass')
-- An actual new dispatch replenishes the request budget even when its table is reused.
ai_register_handler('pr07_reset_budget',function(self)
    for i=1,128 do assert(select(2,self:tryUseCard(inner))=='planned') end
    assert(select(2,self:tryUseCard(inner))=='unsupported')
    return {kind='pass'}
end)
reused.kind='pr07_reset_budget'
assert(ai_decide(reused).kind=='pass' and ai_decide(reused).kind=='pass')

local conv=ConversionView.new({conversion_id=5,name='slash',class_name='Slash',suit=0,number=0,
    activation_skill='paid',activation_instance=2,source_skill='root',source_instance=9,
    subcards={1,2},cost_count=2,eligible_subcards={1,2,3},
    complete_coverage=true,target_combinations={{'a','b'}}})
local bound=assert(conv:withSubcards({2,3}))
assert(conv:getSubcards()[1]==1 and bound:getSubcards()[1]==2)
assert(bound:toCardSpec().conversion_id==5 and bound:toCardSpec().subcards[2]==3)
assert(bound:getSourceInstanceID()==9 and bound:getActivationInstanceId()==2)
assert(conv:withSubcards({1,1})==nil and conv:withSubcards({1,4})==nil and conv:withSubcards({1})==nil)
assert(bound:getTargetSelection({'a'})[1]=='b', 'conversion target path diverged')
-- Failing to bind one instance must not hide a later, authorized cost ticket.
local multiple=request()
multiple.card_conversions={
    {conversion_id=5,name='slash',activation_skill='paid',activation_instance=2,
        source_owner='v',source_skill='root',source_instance=9,
        cost_count=2,eligible_subcards={1,2},subcards={1,2}},
    {conversion_id=6,name='slash',activation_skill='paid',activation_instance=3,
        source_owner='a',source_skill='root',source_instance=10,
        cost_count=2,eligible_subcards={2,3},subcards={2,3}}}
local chosen=assert(SmartAIView.new(multiple):newCard('slash',{subcards={2,3}}))
assert(chosen:getConversionId()==6 and chosen:getActivationInstanceId()==3)
assert(chosen:getSourceOwner()=='a' and chosen:getSourceInstanceID()==10)
assert(SmartAIView.new(multiple):newCard('slash',{instance=2,subcards={2,3}})==nil)
ai_card_use.PR07Inner=nil; ai_card_use.PR07Outer=nil; ai_card_use.PR07Loop=nil
