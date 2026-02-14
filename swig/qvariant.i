class QVariant {
public:
	QVariant();
	QVariant(int);
	QVariant(const char *);
	QVariant(bool);
	int toInt() const;
	QString toString() const;
	QStringList toStringList() const;
	bool toBool() const;
};

%extend QVariant {
	void setValue(int value) {
		$self->setValue(QVariant::fromValue(value));
	}

	void setValue(const char *value) {
		$self->setValue(QVariant::fromValue(QString(value)));
	}

	void setStringList(const char *value) {
		$self->setValue(QVariant::fromValue(QString(value).split("|")));
	}

	void setValue(DamageStruct *damage) {
		$self->setValue(QVariant::fromValue(*damage));
	}

	DamageStruct toDamage() const{
		return $self->value<DamageStruct>();
	}

	void setValue(CardEffectStruct *effect) {
		$self->setValue(QVariant::fromValue(*effect));
	}

	CardEffectStruct toCardEffect() const{
		return $self->value<CardEffectStruct>();
	}

	void setValue(SlashEffectStruct *effect) {
		$self->setValue(QVariant::fromValue(*effect));
	}

	SlashEffectStruct toSlashEffect() const{
		return $self->value<SlashEffectStruct>();
	}

	void setValue(CardUseStruct *use) {
		$self->setValue(QVariant::fromValue(*use));
	}

	CardUseStruct toCardUse() const{
		return $self->value<CardUseStruct>();
	}

	void setValue(const Card *card) {
		$self->setValue(QVariant::fromValue(card));
	}

	const Card *toCard() const{
		return $self->value<const Card *>();
	}

	void setValue(ServerPlayer *player) {
		$self->setValue(QVariant::fromValue(player));
	}

	ServerPlayer *toPlayer() const{
		return $self->value<ServerPlayer *>();
	}

	void setValue(DyingStruct *dying) {
		$self->setValue(QVariant::fromValue(*dying));
	}

	DyingStruct toDying() const{
		return $self->value<DyingStruct>();
	}

	void setValue(DeathStruct *death) {
		$self->setValue(QVariant::fromValue(*death));
	}

	DeathStruct toDeath() const{
		return $self->value<DeathStruct>();
	}

	void setValue(RecoverStruct *recover) {
		$self->setValue(QVariant::fromValue(*recover));
	}

	RecoverStruct toRecover() const{
		return $self->value<RecoverStruct>();
	}

	void setValue(JudgeStruct *judge) {
		$self->setValue(QVariant::fromValue(judge));
	}

	JudgeStruct *toJudge() const{
		return $self->value<JudgeStruct *>();
	}

	void setValue(PindianStruct *pindian) {
		$self->setValue(QVariant::fromValue(pindian));
	}

	PindianStruct *toPindian() const{
		return $self->value<PindianStruct *>();
	}

	void setValue(PhaseChangeStruct *phase) {
		$self->setValue(QVariant::fromValue(*phase));
	}

	PhaseChangeStruct toPhaseChange() const{
		return $self->value<PhaseChangeStruct>();
	}

	void setValue(CardsMoveOneTimeStruct *move) {
		$self->setValue(QVariant::fromValue(*move));
	}

	CardsMoveOneTimeStruct toMoveOneTime() const{
		return $self->value<CardsMoveOneTimeStruct>();
	}

	void setValue(CardResponseStruct *resp) {
		$self->setValue(QVariant::fromValue(*resp));
	}

	CardResponseStruct toCardResponse() const{
		return $self->value<CardResponseStruct>();
	}

	void setValue(QList<int> intlist) {
		QVariantList varlist;
		foreach (int n, intlist)
			varlist.append(QVariant::fromValue(n));
		$self->setValue(QVariant::fromValue(varlist));
	}

	QList<int> toIntList() const{
		QList<int> result;
		if ($self->canConvert<QVariantList>()) {
			foreach (QVariant qv, $self->toList())
				result.append(qv.toInt());
		}
		return result;
	}

	void setValue(MarkStruct *mark) {
		$self->setValue(QVariant::fromValue(*mark));
	}

	MarkStruct toMark() const{
		return $self->value<MarkStruct>();
	}

	void setValue(DrawStruct *draw) {
		$self->setValue(QVariant::fromValue(*draw));
	}

	DrawStruct toDraw() const{
		return $self->value<DrawStruct>();
	}

	void setValue(HpLostStruct *lost) {
		$self->setValue(QVariant::fromValue(*lost));
	}

	HpLostStruct toHpLost() const{
		return $self->value<HpLostStruct>();
	}

	void setValue(MaxHpStruct *maxhp) {
		$self->setValue(QVariant::fromValue(*maxhp));
	}

	MaxHpStruct toMaxHp() const{
		return $self->value<MaxHpStruct>();
	}
};