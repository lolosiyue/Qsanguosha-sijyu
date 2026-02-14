#include "exppattern.h"
#include "engine.h"

ExpPattern::ExpPattern(const QString &exp)
{
    this->exp = exp;
	available = exp.startsWith("$");
	if(available) this->exp.remove("$");
}

bool ExpPattern::match(const Player *player, const Card *card) const
{
	if(available&&player&&!card->isAvailable(player))
		return false;
	if(exp.contains('#')){
		foreach(QString one_exp, exp.split('#'))
			if (matchOne(player, card, one_exp)) return true;
		return false;
	}
	return matchOne(player, card, exp);
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
bool ExpPattern::matchOne(const Player *player, const Card *card, QString one_exp) const
{
    QStringList factors = one_exp.split('|');
	if(factors[0]!="."){
		bool checkpoint = false;
		foreach (QString or_name, factors[0].split(',')) {
			foreach (QString name, or_name.split('+')) {
				bool positive = name.startsWith('^');
				if (positive) name = name.mid(1);
				if (card->getType()==name||card->toString()==name||"%"+card->objectName()==name||card->isKindOf(name.toLocal8Bit().data()))
					checkpoint = !positive;
				else
					checkpoint = positive;
				if (!checkpoint) break;
			}
			if (checkpoint) break;
		}
		if (!checkpoint)
			return false;
	}
	if(factors.length()<2)
		return true;
	
	if(factors[1]!="."){
		bool checkpoint = false;
		foreach (QString suit, factors[1].split(',')) {
			bool positive = suit.startsWith('^');
			if (positive) suit = suit.mid(1);
			if (card->getSuitString() == suit || card->getColorString() == suit)
				checkpoint = !positive;
			else
				checkpoint = positive;
			if (checkpoint) break;
		}
		if (!checkpoint)
			return false;
	}
	if(factors.length()<3)
		return true;
	
	if(factors[2]!="."){
		bool checkpoint = false;
		foreach (QString number, factors[2].split(',')) {
			bool positive = number.startsWith('^');
			if (positive) number = number.mid(1);
			checkpoint = positive;
			if(number.contains('~')){
				int from = 1, to = 13;
				QStringList params = number.split('~');
				if (params[0].size()>0) from = params[0].toInt();
				if (params[1].size()>0) to = params[1].toInt();
				if(card->getNumber() >= from && card->getNumber() <= to)
					checkpoint = !positive;
			}else{
				bool can;
				int n = number.toInt(&can);
				if(can){
					if(n==card->getNumber())
						checkpoint = !positive;
				}else if(number==card->getNumberString())
					checkpoint = !positive;
			}
			if (checkpoint) break;
		}
		if (!checkpoint)
			return false;
	}
	if(factors.length()<4)
		return true;
	
	if(factors[3]!="."&&player){
		bool checkpoint = false;
		foreach (QString place, factors[3].split(',')) {
			bool positive = place.startsWith('^');
			if (positive) place = place.mid(1);
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
	return true;/*

	checkpoint = factors[3] == "." || !player;
    if (!checkpoint){
		QStringList places = factors[3].split(',');
		foreach (int id, card->getSubcards()) {
			checkpoint = false;
			foreach (QString place, places) {
				bool positive = place.startsWith('^');
				if (positive) place = place.mid(1);
				checkpoint = positive;
				if (place == "equipped"){
					if(player->getEquipsId().contains(id))
						checkpoint = !positive;
				}else if (place == "hand"){
					if(player->handCards().contains(id))
						checkpoint = !positive;
				}else if (place.startsWith("%")) {
					place = place.mid(1);
					foreach(const Player *as, player->getAliveSiblings()){
						if (as->getPile(place).contains(id)) {
							checkpoint = !positive;
							break;
						}
					}
				} else{
					if(player->getPile(place).contains(id))
						checkpoint = !positive;
				}
				if (checkpoint)
					break;
			}
			if (!checkpoint)
				break;
        }
    }
    return checkpoint;*/
}