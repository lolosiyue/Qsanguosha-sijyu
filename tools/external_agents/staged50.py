"""Bounded, side-effect-free hierarchy over complete native action tickets.

No engine queries or inferred legal completions. Complete concrete zero/one-card
conversion tickets supply every material, output and target branch. Only the final
result crosses submit; all provider calls reuse the room's adapter and game ID.
"""
import copy

import providers as p

MAX_VISITS = 8
MAX_BACK = 2
MAX_NODES = 192


class Unsupported(Exception):
    """Use native SmartAI for this entire decision."""


class Invalidated(p.DecisionError):
    """Do not submit, retry, or restamp a stale plan."""


class Node:
    def __init__(self, stage, context, branches, action=None):
        self.stage, self.context, self.branches = stage, context, branches
        self.action = action

    def options(self, back):
        choices = {'pick_' + str(i): text for i, (text, _) in enumerate(self.branches)}
        choices['cancel'] = 'Cancel this plan and pass the original optional decision.'
        if back:
            choices['back'] = 'Undo one planning stage; no card or skill has been used.'
        return choices


class Plan:
    def __init__(self, request, state, effects, adapter):
        # Keep an immutable-by-ownership copy, never borrow a mutable poll packet.
        q = copy.deepcopy(request)
        if (q['kind'] not in (0, 1) or q.get('hasSkillActionContext')
                or not q.get('conversionsEnumerated') or not q.get('stagedActionsComplete')):
            raise Unsupported('staged_action_coverage_unknown')
        self.ticket = (q['decisionId'], q['stateRevision'], q['viewerObjectName'])
        self.state = copy.deepcopy(state)
        self.visits = self.calls = self.backs = 0
        self.nodes = []
        own = {c['cardId']: c for c in q['worldView']['handCards']}
        own.update({c['cardId']: c for c in q['worldView']['self'].get('equips', [])})
        names = {v['objectName'] for v in
                 [q['worldView']['self']] + q['worldView']['players']}

        def node(stage, context, branches, action=None):
            if len(self.nodes) >= MAX_NODES:
                raise Unsupported('staged_node_limit')
            result = Node(stage, context, branches, action)
            self.nodes.append(result)
            return result

        def targets(c, name, action, context):
            if name not in effects:
                raise Unsupported('unknown_card_effect')
            if not c.get('completeCoverage'):
                raise Unsupported('incomplete_target_projection')
            groups = ([[]] if c['feasibleWithNoTarget'] else []) + c['targetCombinations']
            seen, branches = set(), []
            for group in groups:
                key = tuple(group)
                if (len(key) > 8 or len(set(key)) != len(key)
                        or any(n not in names for n in key)):
                    raise Unsupported('staged_target_shape')
                if key in seen:
                    continue
                seen.add(key)
                branches.append(('Use on ' + str(list(key) or 'automatic targets'), list(key)))
            if not branches:
                return None  # Complete projection proves no legal completion.
            return node('targets', dict(context, output=name, effect=effects[name]), branches, action)

        root = []
        for c in q['cardCandidates']:
            if not c['available'] or c['limited']:
                continue
            card = own.get(c['cardId'])
            if card is None:
                raise p.DecisionError('candidate_not_owned')
            action = {'candidateId': c['candidateId'], 'useCardId': c['cardId']}
            branch = targets(c, card['objectName'], action, {'material': [c['cardId']]})
            if branch:
                root.append(('Use ' + card['objectName'] + ' card ' + str(c['cardId'])
                             + '. ' + effects[card['objectName']], branch))

        def reference_key(ref):
            if (not isinstance(ref, dict)
                    or not isinstance(ref.get('ownerObjectName'), str) or not ref['ownerObjectName']
                    or not isinstance(ref.get('skillName'), str) or not ref['skillName']
                    or type(ref.get('instanceID')) is not int or ref['instanceID'] <= 0):
                raise p.DecisionError('invalid_conversion_reference')
            return ref['ownerObjectName'], ref['skillName'], ref['instanceID']

        families = {}
        ticket_ids = set()
        for c in q['cardConversions']:
            if not c['available']:
                continue
            if (c.get('costCount', 0) != 0 or len(c['subcardIds']) > 1
                    or not c['activationQuotaAvailable'] or not c['sourceQuotaAvailable']):
                raise Unsupported('staged_conversion_contract_unknown')
            ref, source = c['activationRef'], c['sourceRef']
            activation_key, source_key = reference_key(ref), reference_key(source)
            if activation_key[0] != q['viewerObjectName']:
                raise Unsupported('staged_conversion_owner_unknown')
            if c['conversionId'] in ticket_ids:
                raise p.DecisionError('duplicate_conversion_ticket')
            ticket_ids.add(c['conversionId'])
            material = tuple(c['subcardIds'])
            if any(card_id not in own for card_id in material):
                raise Unsupported('staged_conversion_material_unknown')
            spec = {k: c[k] for k in ('name', 'suit', 'number', 'conversionId', 'subcardIds')}
            spec['skillName'] = ref['skillName']
            family_context = {'skill': ref['skillName'], 'activationRef': ref, 'sourceRef': source}
            context = dict(family_context, material=list(material))
            target = targets(c, c['name'], {'cardSpec': spec}, context)
            if target:
                # Preserve the complete activation and root source, including borrowed
                # owners. Equal materials may expose several already-authorized outputs.
                family = families.setdefault((activation_key, source_key),
                                             {'context': family_context, 'materials': {}})
                outputs = family['materials'].setdefault(material, [])
                outputs.append((c['name'] + ', suit ' + str(c['suit'])
                                + ', number ' + str(c['number']) + '. ' + effects[c['name']], target))
        for (activation, source), family in families.items():
            branches = []
            for material, outputs in family['materials'].items():
                context = dict(family['context'], material=list(material))
                output = node('output', context, outputs)
                description = 'No material cards'
                if material:
                    card_id = material[0]
                    card = own[card_id]
                    description = ('Material ' + str(card_id) + ': ' + card['objectName']
                                   + ', suit ' + str(card['suit']) + ', number ' + str(card['number']))
                branches.append((description, output))
            root.append(('Use ' + activation[1] + ' instance ' + str(activation[2])
                         + ' owned by ' + activation[0] + '; source ' + source[0] + '/'
                         + source[1] + '/' + str(source[2]),
                         node('material', family['context'], branches)))
        self.root = node('action', {}, root)
        # Preflight every reachable stage before the first paid call. A large
        # later branch cannot silently disappear after a material has been chosen.
        for current in self.nodes:
            options = current.options(current is not self.root)
            if len(options) > 255:
                raise Unsupported('staged_options_over_255')
            if len(options) > 1:
                try:
                    adapter.prepare_payload('jev', self.observation(current), options)
                except p.DecisionError as e:
                    if str(e) == 'input_too_large':
                        raise Unsupported('staged_input_limit') from None
                    raise

    def observation(self, node):
        state = copy.deepcopy(self.state)
        state['observation']['planning'] = {
            'stage': node.stage, 'selected': node.context,
            'max_stage_visits': MAX_VISITS, 'max_back': MAX_BACK,
            'no_game_effects_until_native_submit': True}
        return state

    def result(self, action=None):
        return {'decisionId': self.ticket[0], 'stateRevision': self.ticket[1],
                'kind': 'pass' if action is None else 'useCard',
                'action': {} if action is None else copy.deepcopy(action)}

    def valid(self, client, cancelled):
        if cancelled():
            raise Invalidated('staged_plan_cancelled')
        poll = client.call({'op': 'poll'})
        q = poll.get('request')
        if (poll.get('status') != 'waiting-no-clock' or poll.get('lastError') or not q
                or (q['decisionId'], q['stateRevision'], q['viewerObjectName']) != self.ticket):
            raise Invalidated('staged_plan_stale')

    def choose(self, client, adapter, paid=True, cancelled=lambda: False):
        current, history = self.root, []
        while self.visits < MAX_VISITS:
            self.valid(client, cancelled)
            self.visits += 1
            options = current.options(bool(history) and self.backs < MAX_BACK)
            # A single continuation after choosing a family is mechanically forced.
            # A material or output with one continuation needs no provider call.
            if current is not self.root and len(current.branches) == 1:
                choice = 'pick_0'
            elif not current.branches:
                choice = 'cancel'
            elif paid:
                self.calls += 1
                choice = adapter.choose('jev', self.observation(current), options)
            else:
                choice = 'pick_0'
            if choice not in options:
                raise p.DecisionError('staged_choice_not_legal')
            # Provider latency may exceed the native deadline or lose this ticket.
            self.valid(client, cancelled)
            if choice == 'cancel':
                return self.result()
            if choice == 'back':
                current = history.pop()
                self.backs += 1
                continue
            index = int(choice.removeprefix('pick_'))
            selected = current.branches[index][1]
            if current.stage == 'targets':
                return self.result(dict(current.action, selectedTargetNames=selected))
            history.append(current)
            current = selected
        raise Unsupported('staged_visit_limit')
