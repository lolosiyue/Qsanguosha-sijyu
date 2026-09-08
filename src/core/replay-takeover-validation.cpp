#include "replay-takeover-validation.h"
#include "game-snapshot.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <algorithm>

bool validateReplayTakeover(const QString &replayPath, const QString &snapshotPath,
    const QString &seatName, QString *error)
{
	const auto fail = [error](const QString &message) {
		if (error)
			*error = message;
		return false;
	};

	if (snapshotPath.isEmpty() || seatName.isEmpty())
		return fail(QObject::tr("Takeover requires a snapshot and a seat"));

	if (replayPath.isEmpty() || replayPath.endsWith(
		QStringLiteral(".png"), Qt::CaseInsensitive)) {
		return fail(QObject::tr("Takeover is available only for a text replay with snapshots"));
	}

	const QFileInfo snapshotInfo(snapshotPath);
	if (!snapshotInfo.isFile())
		return fail(QObject::tr("Snapshot file does not exist"));

	// The manifest is the pairing boundary between a replay and its snapshots.
	// Replayer performs the hash/schema verification; checking its presence here
	// prevents a direct path from accidentally bypassing that contract.
	const QString snapshotDir = GameSnapshot::getSnapshotDir(replayPath);
	if (QDir::cleanPath(snapshotInfo.absolutePath()) != QDir::cleanPath(snapshotDir))
		return fail(QObject::tr("Snapshot is not in the selected replay's snapshot directory"));
	const QFileInfo manifestInfo(snapshotDir + QLatin1String("/manifest.json"));
	if (!manifestInfo.isFile())
		return fail(QObject::tr("Replay snapshot manifest is missing"));

	QFile manifestFile(manifestInfo.absoluteFilePath());
	if (!manifestFile.open(QIODevice::ReadOnly))
		return fail(QObject::tr("Replay snapshot manifest cannot be opened"));
	QJsonParseError parseError;
	const QJsonDocument manifest = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
	if (parseError.error != QJsonParseError::NoError || !manifest.isObject())
		return fail(QObject::tr("Replay snapshot manifest is invalid"));
	const QJsonObject manifestObject = manifest.object();
	if (manifestObject.value(QStringLiteral("schema")).toString()
		!= QStringLiteral("qsanguosha-takeover-manifest-v1")) {
		return fail(QObject::tr("Replay snapshot manifest schema is unsupported"));
	}
	if (manifestObject.value(QStringLiteral("sessionId")).toString().isEmpty()
		|| !manifestObject.value(QStringLiteral("snapshots")).isArray())
		return fail(QObject::tr("Replay snapshot manifest is incomplete"));

	QFile replayFile(replayPath);
	if (!replayFile.open(QIODevice::ReadOnly))
		return fail(QObject::tr("The source replay cannot be opened"));
	const QByteArray replayHash = QCryptographicHash::hash(
		replayFile.readAll(), QCryptographicHash::Sha256).toHex();
	if (QString::fromLatin1(replayHash)
		!= manifestObject.value(QStringLiteral("replaySha256")).toString()) {
		return fail(QObject::tr("Replay and snapshot manifest do not match"));
	}

	QByteArray snapshotBytes;
	QFile snapshotHashFile(snapshotInfo.absoluteFilePath());
	if (snapshotHashFile.open(QIODevice::ReadOnly))
		snapshotBytes = snapshotHashFile.readAll();
	else
		return fail(QObject::tr("Snapshot cannot be opened"));
	const QString snapshotHash = QString::fromLatin1(
		QCryptographicHash::hash(snapshotBytes, QCryptographicHash::Sha256).toHex());
	bool manifestEntryFound = false;
	QString manifestTurnSerial;
	QString manifestPlayerName;
	int manifestPlayerTurnCount = 0;
	for (const QJsonValue &entryValue : manifestObject.value(QStringLiteral("snapshots")).toArray()) {
		const QJsonObject entry = entryValue.toObject();
		if (entry.value(QStringLiteral("file")).toString() != snapshotInfo.fileName())
			continue;
		if (manifestEntryFound)
			return fail(QObject::tr("Snapshot is listed more than once in the manifest"));
		manifestEntryFound = true;
		if (entry.value(QStringLiteral("sha256")).toString() != snapshotHash)
			return fail(QObject::tr("Snapshot and manifest do not match"));
		manifestTurnSerial = entry.value(QStringLiteral("turnSerial")).toString();
		manifestPlayerName = entry.value(QStringLiteral("playerName")).toString();
		manifestPlayerTurnCount = entry.value(
			QStringLiteral("playerTurnCount")).toInt(0);
	}
	if (!manifestEntryFound)
		return fail(QObject::tr("Snapshot is not listed in the replay manifest"));

	GameSnapshot snapshot(snapshotPath);
	if (!snapshot.isEligible())
		return fail(snapshot.getError().isEmpty()
			? QObject::tr("This snapshot is not eligible for takeover")
			: snapshot.getError());

	const GlobalSnapshot state = snapshot.getState();
	int expectedPlayerTurnCount = 1;
	bool currentPlayerFound = false;
	for (const PlayerSnapshot &player : state.players) {
		if (player.objectName != state.currentPlayer)
			continue;
		expectedPlayerTurnCount = player.marks.value(
			QStringLiteral("Global_TurnCount"), 0) + 1;
		currentPlayerFound = true;
		break;
	}
	if (manifestTurnSerial != QString::number(snapshot.getTurnSerial())
		|| !currentPlayerFound || manifestPlayerName != state.currentPlayer
		|| manifestPlayerTurnCount != expectedPlayerTurnCount)
		return fail(QObject::tr("Snapshot timeline identity does not match the manifest"));
	if (!state.unsupportedState.isEmpty() || state.players.isEmpty())
		return fail(QObject::tr("Snapshot contains unsupported or incomplete state"));
	if (state.currentPlayer.isEmpty() || !state.seatOrder.contains(state.currentPlayer))
		return fail(QObject::tr("Snapshot has no valid current player"));

	QString compatibilityError;
	if (!GameSnapshot::validateRuntimeCompatibility(state, &compatibilityError))
		return fail(compatibilityError);

	const auto playerIt = std::find_if(state.players.cbegin(), state.players.cend(),
		[&seatName](const PlayerSnapshot &player) {
			return player.objectName == seatName;
		});
	if (playerIt == state.players.cend())
		return fail(QObject::tr("Selected seat is not present in the snapshot"));
	if (!playerIt->alive)
		return fail(QObject::tr("A dead seat cannot be selected for takeover"));

	return true;
}
