#include "kof-arrange-controller.h"

#include "button.h"
#include "carditem.h"
#include "client.h"
#include "clientplayer.h"
#include "qsan-selectable-item.h"
#include "server-info.h"
#include "skin-bank.h"
#include "util.h"

#include <QGraphicsRectItem>
#include <QGraphicsScene>

KofArrangeController::KofArrangeController(QGraphicsScene *scene, QObject *parent)
    : QObject(parent)
    , m_scene(scene)
    , selector_box(nullptr)
    , to_change(nullptr)
    , arrange_button(nullptr)
{
}

void KofArrangeController::setTableCenter(const QPointF &center)
{
    m_tableCenter = center;
}

void KofArrangeController::setPendingGeneralChange(CardItem *item)
{
    to_change = item;
}

bool KofArrangeController::isArranging() const
{
    return arrange_button != nullptr;
}

void KofArrangeController::cancelTouchPreviews()
{
    for (CardItem *item : general_items) {
        if (item)
            item->cancelTouchPreview();
    }
}

void KofArrangeController::autoCompleteArrangement()
{
    arrange_items << down_generals.mid(0, 3 - arrange_items.length());
    finishArrange();
}

void KofArrangeController::fillGenerals1v1(const QStringList&names)
{
	int len = names.length()/2;
	QString path = QString("image/system/1v1/select%1.png").arg(len==5 ? "" : "2");
	selector_box = new QSanSelectableItem(path,true);
	selector_box->setFlag(QGraphicsItem::ItemIsMovable);
	selector_box->setPos(m_tableCenter);
	m_scene->addItem(selector_box);
	selector_box->setZValue(10);

	const static int start_x = 42+G_COMMON_LAYOUT.m_cardNormalWidth/2;
	const static int width = 86;
	const static int start_y = 59+G_COMMON_LAYOUT.m_cardNormalHeight/2;
	const static int height=121;

	foreach (QString name,names){
		CardItem*item = new CardItem(name);
		item->setObjectName(name);
		general_items << item;
	}

	qsanShuffle(general_items);

	int n = names.length();
	double scaleRatio = 116.0/G_COMMON_LAYOUT.m_cardNormalHeight;
	for (int i = 0;i < n;i++){
		int row,column;
		if(i < len){
			row = 1;
			column = i;
		} else {
			row = 2;
			column = i-len;
		}

		CardItem*general_item = general_items.at(i);
		general_item->scaleSmoothly(scaleRatio);
		general_item->setParentItem(selector_box);
		general_item->setPos(start_x+width*column,start_y+height*row);
		general_item->setHomePos(general_item->pos());
	}
}

void KofArrangeController::fillGenerals3v3(const QStringList&names)
{
	QString temperature;
	if(Self->getRole().startsWith("l"))
		temperature = "warm";
	else
		temperature = "cool";

	QString path = QString("image/system/3v3/select-%1.png").arg(temperature);
	selector_box = new QSanSelectableItem(path,true);
	selector_box->setFlag(QGraphicsItem::ItemIsMovable);
	m_scene->addItem(selector_box);
	selector_box->setZValue(10);
	selector_box->setPos(m_tableCenter);

	const static int start_x = 109;
	const static int width = 86;
	const static int row_y[4] = { 150,271,394,516 };

	int n = names.length();
	double scaleRatio = 116.0/G_COMMON_LAYOUT.m_cardNormalHeight;
	for (int i = 0;i < n;i++){
		int row,column;
		if(i < 8){
			row = 1;
			column = i;
		} else {
			row = 2;
			column = i-8;
		}

		CardItem*general_item = new CardItem(names.at(i));
		general_item->scaleSmoothly(scaleRatio);
		general_item->setParentItem(selector_box);
		general_item->setPos(start_x+width*column,row_y[row]);
		general_item->setHomePos(general_item->pos());
		general_item->setObjectName(names.at(i));

		general_items << general_item;
	}
}

void KofArrangeController::fillGenerals(const QStringList&names)
{
	if(ServerInfo.GameMode=="06_3v3")
		fillGenerals3v3(names);
	else if(ServerInfo.GameMode=="02_1v1")
		fillGenerals1v1(names);
}


void KofArrangeController::takeGeneral(const QString&who,const QString&name,const QString&rule)
{
	bool self_taken;
	if(who=="warm")
		self_taken = Self->getRole().startsWith("l");
	else
		self_taken = Self->getRole().startsWith("r");
	QList<CardItem*>*to_add = self_taken ?&down_generals :&up_generals;

	CardItem*general_item = nullptr;
	foreach (CardItem*item,general_items){
		if(item->objectName()==name){
			general_item = item;
			break;
		}
	}

	Q_ASSERT(general_item);

	general_item->disconnect(this);
	general_items.removeOne(general_item);
	to_add->append(general_item);

	int x,y;
	if(ServerInfo.GameMode=="06_3v3"){
		x = 63+(to_add->length()-1)*(148-62);
		y = self_taken ? 452 : 85;
	} else {
		x = 43+(to_add->length()-1)*86;
		y = self_taken ? 60+120*3 : 60;
	}
	x = x+G_COMMON_LAYOUT.m_cardNormalWidth/2;
	y = y+G_COMMON_LAYOUT.m_cardNormalHeight/2;
	general_item->setHomePos(QPointF(x,y));
	general_item->goBack(true);

	if(((ServerInfo.GameMode=="06_3v3"&&Self->getRole()!="lord"&&Self->getRole()!="renegade")
		|| (ServerInfo.GameMode=="02_1v1"&&rule=="2013"))
		&&general_items.isEmpty()){
		if(selector_box){
			selector_box->hide();
			delete selector_box;
			selector_box = nullptr;
		}
	}
}

void KofArrangeController::recoverGeneral(int index,const QString&name)
{
	QString obj_name = QString("x%1").arg(index);
	foreach (CardItem*item,general_items){
		if(item->objectName()==obj_name){
			item->changeGeneral(name);
			break;
		}
	}
}

void KofArrangeController::startGeneralSelection()
{
	foreach (CardItem*item,general_items){
		item->setFlag(QGraphicsItem::ItemIsFocusable);
		connect(item,SIGNAL(double_clicked()),this,SLOT(selectGeneral()));
		connect(item,SIGNAL(touchPreviewRequested(CardItem *)),this,SIGNAL(touchPreviewRequested(CardItem *)));
	}
}

void KofArrangeController::selectGeneral()
{
	CardItem*item = qobject_cast<CardItem*>(sender());
	if(item){
		ClientInstance->onPlayerChooseDraftGeneral(item->objectName());
		foreach (CardItem*item,general_items){
			item->setFlag(QGraphicsItem::ItemIsFocusable,false);
			item->disconnect(this);
		}
		ClientInstance->setStatus(Client::NotActive);
	}
}

void KofArrangeController::changeGeneral(const QString&general)
{
	if(to_change&&arrange_button) to_change->changeGeneral(general);
}

void KofArrangeController::startArrange(const QString&to_arrange)
{
	arrange_items.clear();
	QString mode;
	QList<QPointF> positions;
	if(ServerInfo.GameMode=="06_3v3"){
		mode = "3v3";
		positions << QPointF(279,356) << QPointF(407,356) << QPointF(535,356);
	} else if(ServerInfo.GameMode=="02_1v1"){
		mode = "1v1";
		if(down_generals.length()==5)
			positions << QPointF(130,335) << QPointF(260,335) << QPointF(390,335);
		else
			positions << QPointF(173,335) << QPointF(303,335) << QPointF(433,335);
	}

	if(ServerInfo.GameMode=="06_XMode"){
		QStringList arrangeList = to_arrange.split("+");
		if(arrangeList.length()==5)
			positions << QPointF(130,335) << QPointF(260,335) << QPointF(390,335);
		else
			positions << QPointF(173,335) << QPointF(303,335) << QPointF(433,335);
		QString path = QString("image/system/XMode/arrange%1.png").arg((arrangeList.length()==5) ? 1 : 2);
		selector_box = new QSanSelectableItem(path,true);
		selector_box->setFlag(QGraphicsItem::ItemIsMovable);
		selector_box->setPos(m_tableCenter);
		m_scene->addItem(selector_box);
		selector_box->setZValue(10);
	} else {
		QString suffix = (mode=="1v1"&&down_generals.length()==6) ? "2" : "";
		QString path = QString("image/system/%1/arrange%2.png").arg(mode).arg(suffix);
		selector_box->load(path);
		selector_box->setPos(m_tableCenter);
	}

	if(ServerInfo.GameMode=="06_XMode"){
		Q_ASSERT(!to_arrange.isEmpty());
		down_generals.clear();
		foreach (QString name,to_arrange.split("+")){
			CardItem*item = new CardItem(name);
			item->setObjectName(name);
			item->scaleSmoothly(116.0/G_COMMON_LAYOUT.m_cardNormalHeight);
			item->setParentItem(selector_box);
			int x = 43+down_generals.length()*86;
			int y = 60+120*3;
			x = x+G_COMMON_LAYOUT.m_cardNormalWidth/2;
			y = y+G_COMMON_LAYOUT.m_cardNormalHeight/2;
			item->setPos(x,y);
			item->setHomePos(QPointF(x,y));
			down_generals << item;
		}
	}
	foreach (CardItem*item,down_generals){
		item->setFlag(QGraphicsItem::ItemIsFocusable);
		item->setAutoBack(false);
		connect(item,SIGNAL(released()),this,SLOT(toggleArrange()));
	}

	static QRect rect(0,0,80,120);

	foreach (QPointF pos,positions){
		QGraphicsRectItem*rect_item = new QGraphicsRectItem(rect,selector_box);
		rect_item->setPos(pos);
		rect_item->setPen(Qt::NoPen);
		arrange_rects << rect_item;
	}

	arrange_button = new Button(tr("Complete"),0.8);
	arrange_button->setParentItem(selector_box);
	arrange_button->setPos(600,330);
	connect(arrange_button,SIGNAL(clicked()),this,SLOT(finishArrange()));
}

void KofArrangeController::toggleArrange()
{
	CardItem*item = qobject_cast<CardItem*>(sender());
	if(item==nullptr) return;

	QGraphicsItem*arrange_rect = nullptr;
	int index = -1;
	for (int i = 0;i < 3;i++){
		QGraphicsItem*rect = arrange_rects.at(i);
		if(item->collidesWithItem(rect)){
			arrange_rect = rect;
			index = i;
		}
	}

	if(arrange_rect==nullptr){
		if(arrange_items.contains(item)){
			arrange_items.removeOne(item);
			down_generals << item;
		}
	} else {
		arrange_items.removeOne(item);
		down_generals.removeOne(item);
		arrange_items.insert(index,item);
	}

	int n = qMin(arrange_items.length(),3);
	for (int i = 0;i < n;i++){
		QPointF pos = arrange_rects.at(i)->pos();
		CardItem*item = arrange_items.at(i);
		item->setHomePos(pos);
		item->goBack(true);
	}

	while (arrange_items.length() > 3){
		CardItem*last = arrange_items.takeLast();
		down_generals << last;
	}

	for (int i = 0;i < down_generals.length();i++){
		QPointF pos;
		if(ServerInfo.GameMode=="06_3v3")
			pos = QPointF(65+G_COMMON_LAYOUT.m_cardNormalWidth/2+i*86,
			452+G_COMMON_LAYOUT.m_cardNormalHeight/2);
		else
			pos = QPointF(43+G_COMMON_LAYOUT.m_cardNormalWidth/2+i*86,
			60+G_COMMON_LAYOUT.m_cardNormalHeight/2+3*120);
		CardItem*item = down_generals.at(i);
		item->setHomePos(pos);
		item->goBack(true);
	}
}

void KofArrangeController::finishArrange()
{
	if(arrange_items.length()!=3) return;

	arrange_button->deleteLater();

	QStringList names;
	foreach(CardItem*item,arrange_items)
		names << item->objectName();

	if(selector_box)
		selector_box->deleteLater();

	arrange_rects.clear();

	ClientInstance->onPlayerArrangeGenerals(names);
}