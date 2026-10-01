#include "exppattern.h"
#include "engine.h"

ExpPattern::ExpPattern(const QString &exp)
{
    this->exp = exp;
	available = exp.startsWith("$");
	if(available) this->exp.remove("$");

    // '|' means 'and', '#' means 'or'. See matchOne() for the meaning of each part.
    foreach (const QString &one_exp, this->exp.split('#')) {
        Alternative alternative;
        const QStringList factors = one_exp.split('|');
        alternative.factorCount = factors.length();
        alternative.anyName = factors[0] == ".";
        if (!alternative.anyName) {
            foreach (const QString &or_name, factors[0].split(',')) {
                QList<Term> group;
                foreach (QString name, or_name.split('+')) {
                    Term term;
                    term.negated = name.startsWith('^');
                    if (term.negated) name = name.mid(1);
                    term.text = name;
                    term.className = name.toLocal8Bit();
                    group << term;
                }
                alternative.names << group;
            }
        }
        if (factors.length() >= 2) {
            alternative.anySuit = factors[1] == ".";
            if (!alternative.anySuit) {
                foreach (QString suit, factors[1].split(',')) {
                    Term term;
                    term.negated = suit.startsWith('^');
                    if (term.negated) suit = suit.mid(1);
                    term.text = suit;
                    alternative.suits << term;
                }
            }
        }
        if (factors.length() >= 3) {
            alternative.anyNumber = factors[2] == ".";
            if (!alternative.anyNumber) {
                foreach (QString number, factors[2].split(',')) {
                    NumberTerm term;
                    term.negated = number.startsWith('^');
                    if (term.negated) number = number.mid(1);
                    term.text = number;
                    if (number.contains('~')) {
                        term.isRange = true;
                        const QStringList params = number.split('~');
                        if (params[0].size() > 0) term.from = params[0].toInt();
                        if (params[1].size() > 0) term.to = params[1].toInt();
                    } else {
                        term.value = number.toInt(&term.isInt);
                    }
                    alternative.numbers << term;
                }
            }
        }
        if (factors.length() >= 4) {
            alternative.anyPlace = factors[3] == ".";
            if (!alternative.anyPlace) {
                foreach (QString place, factors[3].split(',')) {
                    Term term;
                    term.negated = place.startsWith('^');
                    if (term.negated) place = place.mid(1);
                    term.text = place;
                    alternative.places << term;
                }
            }
        }
        alternatives << alternative;
    }
}

bool ExpPattern::match(const Player *player, const Card *card) const
{
	if(available&&player&&!card->isAvailable(player))
		return false;
	foreach (const Alternative &alternative, alternatives)
		if (matchOne(player, card, alternative)) return true;
	return false;
}

// '|' means 'and', '#' means 'or'.
// the expression splited by '|' has 4 parts,
// 1st part means the card name, and ',' means more than one options.
// 2nd patt means the card suit, and ',' means more than one options.
// 3rd part means the card number, and ',' means more than one options,
// the number uses '~' to make a scale for valid expressions
// 4th part means the card place, and ',' means more than one options,
// "hand" stands for handcard and "equipped" stands for the cards in the placeequip
// if it is neigher "hand" nor "equipped", it stands for the pile the card is in.
bool ExpPattern::matchOne(const Player *player, const Card *card, const Alternative &alternative) const
{
	if(!alternative.anyName){
		// Card 字串只在型別比對失敗時才組，和原本的短路順序一致。
		const QString type = card->getType();
		const QString percentName = "%" + card->objectName();
		QString cardString;
		bool cardStringReady = false;
		bool checkpoint = false;
		foreach (const QList<Term> &group, alternative.names) {
			foreach (const Term &term, group) {
				bool hit = type == term.text;
				if (!hit) {
					if (!cardStringReady) {
						cardString = card->toString();
						cardStringReady = true;
					}
					hit = cardString == term.text || percentName == term.text
						|| card->isKindOf(term.className.constData());
				}
				checkpoint = hit ? !term.negated : term.negated;
				if (!checkpoint) break;
			}
			if (checkpoint) break;
		}
		if (!checkpoint)
			return false;
	}
	if(alternative.factorCount<2)
		return true;

	if(!alternative.anySuit){
		const QString suitString = card->getSuitString();
		const QString colorString = card->getColorString();
		bool checkpoint = false;
		foreach (const Term &suit, alternative.suits) {
			if (suitString == suit.text || colorString == suit.text)
				checkpoint = !suit.negated;
			else
				checkpoint = suit.negated;
			if (checkpoint) break;
		}
		if (!checkpoint)
			return false;
	}
	if(alternative.factorCount<3)
		return true;

	if(!alternative.anyNumber){
		bool checkpoint = false;
		foreach (const NumberTerm &number, alternative.numbers) {
			checkpoint = number.negated;
			if(number.isRange){
				if(card->getNumber() >= number.from && card->getNumber() <= number.to)
					checkpoint = !number.negated;
			}else if(number.isInt){
				if(number.value==card->getNumber())
					checkpoint = !number.negated;
			}else if(number.text==card->getNumberString())
				checkpoint = !number.negated;
			if (checkpoint) break;
		}
		if (!checkpoint)
			return false;
	}
	if(alternative.factorCount<4)
		return true;

	if(!alternative.anyPlace&&player){
		bool checkpoint = false;
		foreach (const Term &term, alternative.places) {
			const bool positive = term.negated;
			const QString &place = term.text;
			foreach (int id, card->getSubcards()) {
				checkpoint = positive;
				if (place == "equipped"){
					if(player->getEquipsId().contains(id))
						checkpoint = !positive;
				}else if (place == "hand"){
					if(player->handCards().contains(id))
						checkpoint = !positive;
				}else if (place.startsWith("%")) {
					QString place2 = place.mid(1);
					foreach(const Player *as, player->getAliveSiblings()){
						if (as->getPile(place2).contains(id)) {
							checkpoint = !positive;
							break;
						}
					}
				} else {
					if(player->getPile(place).contains(id))
						checkpoint = !positive;
				}
				if(!checkpoint)
					break;
			}
			if(checkpoint)
				return true;
		}
		return false;
	}
	return true;
}
