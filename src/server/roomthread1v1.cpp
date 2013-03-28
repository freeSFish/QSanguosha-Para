#include "roomthread1v1.h"
#include "room.h"
#include "engine.h"
#include "settings.h"
#include "generalselector.h"
#include "jsonutils.h"

#include <QDateTime>

using namespace QSanProtocol;
using namespace QSanProtocol::Utils;

RoomThread1v1::RoomThread1v1(Room *room)
    : room(room)
{
    room->getRoomState()->reset();
}

void RoomThread1v1::run() {
    // initialize the random seed for this thread
    qsrand(QTime(0, 0, 0).secsTo(QTime::currentTime()));

    QSet<QString> banset = Config.value("Banlist/1v1").toStringList().toSet();
    general_names = Sanguosha->getRandomGenerals(10, banset);

    QStringList known_list = general_names.mid(0, 6);
    unknown_list = general_names.mid(6, 4);

    for (int i = 0; i < 4; i++)
        general_names[i + 6] = QString("x%1").arg(QString::number(i));

    room->doBroadcastNotify(S_COMMAND_FILL_GENERAL, toJsonArray(known_list << "x0" << "x1" << "x2" << "x3"));

    ServerPlayer *first = room->getPlayers().at(0), *next = room->getPlayers().at(1);
    askForTakeGeneral(first);

    while (general_names.length() > 1) {
        qSwap(first, next);

        askForTakeGeneral(first);
        askForTakeGeneral(first);
    }

    askForTakeGeneral(next);

    startArrange(first);
    startArrange(next);
}

void RoomThread1v1::askForTakeGeneral(ServerPlayer *player) {
    while (room->isPaused()) {}

    QString name;
    if (general_names.length() == 1)
        name = general_names.first();
    else if (player->getState() != "online")
        name = GeneralSelector::getInstance()->select1v1(general_names);

    if (name.isNull()) {
        bool success = room->doRequest(player, S_COMMAND_ASK_GENERAL, Json::Value::null, true);
        Json::Value clientReply = player->getClientReply();
        if (success && clientReply.isString()) {
            name = toQString(clientReply.asCString());
            takeGeneral(player, name);
        } else {
            GeneralSelector *selector = GeneralSelector::getInstance();
            name = selector->select1v1(general_names);
            takeGeneral(player, name);
        }
    } else {
        msleep(Config.AIDelay);
        takeGeneral(player, name);
    }
}

void RoomThread1v1::takeGeneral(ServerPlayer *player, const QString &name) {
    QString group = player->isLord() ? "warm" : "cool";
    room->doBroadcastNotify(room->getOtherPlayers(player, true), S_COMMAND_TAKE_GENERAL, toJsonArray(group, name));

    QRegExp unknown_rx("x(\\d)");
    QString general_name = name;
    if (unknown_rx.exactMatch(name)) {
        int index = unknown_rx.capturedTexts().at(1).toInt();
        general_name = unknown_list.at(index);

        Json::Value arg(Json::arrayValue);
        arg[0] = index;
        arg[1] = toJsonString(general_name);
        room->doNotify(player, S_COMMAND_RECOVER_GENERAL, arg);
    }

    room->doNotify(player, S_COMMAND_TAKE_GENERAL, toJsonArray(group, general_name));

    QString namearg = unknown_rx.exactMatch(name) ? "anjiang" : name;
    foreach (ServerPlayer *p, room->getPlayers()) {
        LogMessage log;
        log.type = "#VsTakeGeneral";
        log.arg = group;
        log.arg2 = (p == player) ? general_name : namearg;
        room->doNotify(p, S_COMMAND_LOG_SKILL, log.toJsonValue());
    }

    general_names.removeOne(name);
    player->addToSelected(general_name);
}

void RoomThread1v1::startArrange(ServerPlayer *player) {
    while (room->isPaused()) {}

    if (player->getState() != "online") {
        GeneralSelector *selector = GeneralSelector::getInstance();
        arrange(player, selector->arrange1v1(player));
    } else {
        bool success = room->doRequest(player, S_COMMAND_ARRANGE_GENERAL, Json::Value::null, true);
        Json::Value clientReply = player->getClientReply();
        if (success && clientReply.isArray() && clientReply.size() == 3) {
            QStringList arranged;
            tryParse(clientReply, arranged);
            arrange(player, arranged);
        } else {
            GeneralSelector *selector = GeneralSelector::getInstance();
            arrange(player, selector->arrange1v1(player));
        }
    }
}

void RoomThread1v1::arrange(ServerPlayer *player, const QStringList &arranged) {
    Q_ASSERT(arranged.length() == 3);

    QStringList left = arranged.mid(1, 2);
    player->tag["1v1Arrange"] = QVariant::fromValue(left);
    player->setGeneralName(arranged.first());

    foreach (QString general, arranged)
        room->doNotify(player, S_COMMAND_REVEAL_GENERAL, toJsonArray(player->objectName(), general));
}

