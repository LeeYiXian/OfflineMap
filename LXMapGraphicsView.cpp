#include "LXMapGraphicsView.h"

#include "bingformula.h"
#include <QDebug>
#include <QGraphicsItem>
#include <QMouseEvent>
#include <QScrollBar>
#include <QWheelEvent>
#include <QtMath>
#include <QTimer>
#include <QApplication>
#include <QVBoxLayout>
#include <QLabel>
#include <QScreen>
#include <QFileDialog>
#include <QtConcurrent>
#include "mapoverlaywidget.h"

LXMapGraphicsView::LXMapGraphicsView(QWidget* parent)
    : QGraphicsView(parent)
{
    m_scene = new QGraphicsScene();
    this->setScene(m_scene);
    this->setDragMode(QGraphicsView::NoDrag);
    this->setCursor(Qt::ArrowCursor);
    this->setMouseTracking(true);                       // 开启鼠标追踪

    this->setTransformationAnchor(QGraphicsView::NoAnchor);
    this->setResizeAnchor(QGraphicsView::NoAnchor);
    this->setSceneRect(m_scene->sceneRect());

    connect(this, &LXMapGraphicsView::updateImage, this, &LXMapGraphicsView::drawImg);

    // 滚动时 overlay/信息框要跟着走
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this](){
        syncOverlayGeometry();
        updateTargetInfoPanel();
    });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](){
        syncOverlayGeometry();
        updateTargetInfoPanel();
    });
}

LXMapGraphicsView::~LXMapGraphicsView() {}

/**
 * @brief       缩放后设置场景大小范围
 * @param rect
 */
void LXMapGraphicsView::setRect(QRect rect)
{
    m_scene->setSceneRect(rect);

    // 计算最小缩放倍率（防止缩成一个点）
    QSizeF sceneSize = rect.size();
    QSizeF viewSize  = this->viewport()->size();

    if (!sceneSize.isEmpty())
    {
        double sx = viewSize.width()  / sceneSize.width();
        double sy = viewSize.height() / sceneSize.height();
        m_minScale = qMax(sx, sy);
    }

    getShowRect();
    recalcMinScale();
}

/**
 * @brief       绘制瓦片图
 * @param info
 */
void LXMapGraphicsView::drawImg(const ImageInfo& info)
{
    constexpr int TILE_SIZE = 256;

    // 1️⃣ 计算瓦片像素坐标
    QPointF pos = Bing::tileXYToPixelXY(QPoint(info.x, info.y));

    // 2️⃣ 绘制瓦片图像
    QGraphicsPixmapItem* imgItem = m_scene->addPixmap(info.img);
    imgItem->setPos(pos);
    imgItem->setZValue(0);   // 底层

    // 3️⃣ 绘制瓦片边框
    QGraphicsRectItem* rectItem =
        m_scene->addRect(QRectF(pos, QSizeF(TILE_SIZE, TILE_SIZE)));

    QPen pen(QColor(255, 0, 0, 120));   // 半透明红色
    pen.setWidth(1);
    rectItem->setPen(pen);
    rectItem->setBrush(Qt::NoBrush);
    rectItem->setZValue(1);             // 在图片之上
}


/**
 * @brief 清空所有瓦片
 */
void LXMapGraphicsView::clear()
{
    m_scene->clear();
}


void LXMapGraphicsView::wheelEvent(QWheelEvent* event)
{
	recalcMinScale();

	constexpr double zoomFactor = 1.25;

	double currentScale = transform().m11();
	double targetScale = currentScale;

	if (event->angleDelta().y() > 0)
		targetScale *= zoomFactor;
	else
		targetScale /= zoomFactor;

	// 1️⃣ 缩放边界限制
	if (targetScale < m_minScale)
		return;
	if (targetScale > m_maxScale)
		return;

	// Qt5.15+：pos() 弃用，改用 position()
	const QPoint  viewPos = event->position().toPoint();

	// 2️⃣ 记录缩放前鼠标 scene 坐标
	QPointF scenePosBefore = mapToScene(viewPos);

	// 3️⃣ 执行缩放
	double factor = targetScale / currentScale;
	scale(factor, factor);

	// 4️⃣ 保持鼠标下的点不动
	QPointF scenePosAfter = mapToScene(viewPos);
	QPointF delta = scenePosAfter - scenePosBefore;
	translate(delta.x(), delta.y());

	syncOverlayGeometry();
	updateTargetInfoPanel();

	event->accept();
}



/**
 * @brief 获取当前场景的显示范围（场景坐标系）
 */
void LXMapGraphicsView::getShowRect()
{
    QRect rect;
    rect.setTopLeft(this->mapToScene(0, 0).toPoint());
    rect.setBottomRight(this->mapToScene(this->width(), this->height()).toPoint());
}

void LXMapGraphicsView::setCenterLonLat(double lon, double lat)
{
    constexpr int zoom = 17;

    // 1️⃣ 经纬度 → 像素
    centerPos  = Bing::latLongToPixelXY(lon, lat, zoom);

    centerOn(centerPos);
    getShowRect();

    ensureOverlay();

    // ✅ 把雷达范围线参数交给透明层
    QVector<double> rings;
    for (double r = 300.0; r <= 2400.0; r += 300.0)
        rings.push_back(r);

    m_overlay->setRadarParams(centerPos, lat, rings, /*crossArmMeters=*/2400.0);
    m_overlay->update();

    // =================== 雷达目标模拟（最简版） ===================
    static QTimer* simTimer = nullptr;

    if (!simTimer)
    {
        simTimer = new QTimer(this);

        connect(simTimer, &QTimer::timeout, this,[this, lat]()
        {
            // 🔹 静态变量，保证连续性
            static double azimuth = 30.0;   // °
            static double range   = 800.0;  // m

            // 🔹 简单运动模型
            azimuth += 0.4;     // 每帧转动
            range   += 8.0;     // 向外飞

            if (azimuth >= 360.0)
                azimuth -= 360.0;

            if (range > 2400.0)
                range = 600.0;

            // 🔹 绘制一帧
            drawRadarTarget(RadarTargetData(1, azimuth,0.0,range,lat));

        });

        simTimer->start(200);   // 200 ms 一帧
    }
    // ===============================================================

    // =================== 第二个目标模拟 ===================
    static QTimer* simTimer2 = nullptr;
    if (!simTimer2)
    {
        simTimer2 = new QTimer(this);
        connect(simTimer2, &QTimer::timeout, this, [this, lat]()
        {
            static double azimuth2 = 120.0;
            static double range2   = 1000.0;

            azimuth2 -= 0.3;  // 逆向转动
            range2   += 5.0;  // 向外飞

            if (azimuth2 < 0.0) azimuth2 += 360.0;
            if (range2 > 2400.0) range2 = 700.0;

            drawRadarTarget(RadarTargetData(2, azimuth2, 0.0, range2, lat));
        });
        simTimer2->start(200);
    }
}

void LXMapGraphicsView::drawCenterCross(const QPointF& centerPixel,
                                      double armLengthMeters,
                                      double centerLatDeg)
{
    constexpr int ZOOM = 17;

    // 1️⃣ 米 → 像素
    double metersPerPixel =
        Bing::groundResolution(centerLatDeg, ZOOM);

    double armPx = armLengthMeters / metersPerPixel;

    // 2️⃣ 画横线
    QPen pen(QColor(255, 255, 0, 200));   // 黄色准星
    pen.setWidth(2);

    QGraphicsLineItem* hLine =
        m_scene->addLine(
            centerPixel.x() - armPx,
            centerPixel.y(),
            centerPixel.x() + armPx,
            centerPixel.y(),
            pen
            );

    // 3️⃣ 画竖线
    QGraphicsLineItem* vLine =
        m_scene->addLine(
            centerPixel.x(),
            centerPixel.y() - armPx,
            centerPixel.x(),
            centerPixel.y() + armPx,
            pen
            );

    hLine->setZValue(60);
    vLine->setZValue(60);
}

void LXMapGraphicsView::setCenterScene(const QPointF& scenePos)
{
    QRectF viewRect = viewport()->rect();
    QPointF viewCenter = mapToScene(viewRect.center().toPoint());

    QPointF delta = scenePos - viewCenter;
    translate(delta.x(), delta.y());

    getShowRect();
}

void LXMapGraphicsView::drawRadarCircle(const QPointF& centerPixel,
                                      double radiusMeters,
                                      double centerLatDeg)
{
    constexpr int ZOOM = 17;

    //使用你已有的 Bing 地面分辨率函数
    const double metersPerPixel =
        Bing::groundResolution(centerLatDeg, ZOOM);

    const double radiusPx = radiusMeters / metersPerPixel;

    //生成 scene 坐标下的圆
    QRectF rect(
        centerPixel.x() - radiusPx,
        centerPixel.y() - radiusPx,
        radiusPx * 2.0,
        radiusPx * 2.0
        );

    // 3️⃣ 雷达样式（工程友好）
    QPen pen(QColor(0, 255, 0, 160));   // 雷达绿
    pen.setWidth(2);
    pen.setStyle(Qt::DashLine);

    // 4️⃣ 绘制
    QGraphicsEllipseItem* circle =
        m_scene->addEllipse(rect, pen, Qt::NoBrush);

    circle->setZValue(50);
}


void LXMapGraphicsView::drawRadarTarget(RadarTargetData target)
{
    ensureOverlay();

    // 1) 缓存最新数据
    m_radarNewTargets[target.targetId] = target;

    // 2) 计算 scene 坐标（像素）
    QPointF scenePos = calcTargetScenePos(target);
    m_targetScenePos[target.targetId] = scenePos;

    // 3) 喂给 overlay（最新点 + 航迹点）
    m_overlay->setTargets(m_radarNewTargets);
    m_overlay->setTargetScenePos(target.targetId, scenePos);
    m_overlay->appendTrackPoint(target.targetId, scenePos);   // ✅ 新增：航迹点入队
    m_overlay->setSelectedTarget(m_selectedTargetId);

    // 4) 如果是当前选中目标，发引导 & 更新信息框
    if (m_selectedTargetId == target.targetId)
    {
        emit sgnTargetGuide(target.azimuthDeg, target.elevationDeg);
        updateTargetInfoPanel();
    }

    m_overlay->checkAlertZones(target.targetId, scenePos);
}



void LXMapGraphicsView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_leftPressed = true;
        m_isDragging  = false;
        m_pressPos    = event->pos();
        m_lastPos     = event->pos();

    }

    QGraphicsView::mousePressEvent(event);
}

void LXMapGraphicsView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_leftPressed)
    {
        if (!m_isDragging)
        {
            // 判断拖拽阈值
            if ((event->pos() - m_pressPos).manhattanLength() > QApplication::startDragDistance())
            {
                m_isDragging = true;
                setCursor(Qt::ClosedHandCursor); // 开始拖拽
            }
        }

        if (m_isDragging)
        {
            // 计算移动偏移，反方向滚动scrollbar
            QPoint delta = event->pos() - m_lastPos;
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
            verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
            m_lastPos = event->pos();
        }
    }

    QGraphicsView::mouseMoveEvent(event);
}

void LXMapGraphicsView::mouseReleaseEvent(QMouseEvent* event)
{
    bool click = (event->button() == Qt::LeftButton) &&
                 ((event->pos() - m_pressPos).manhattanLength() < DRAG_THRESHOLD);

    m_leftPressed = false;
    m_isDragging  = false;
    setCursor(Qt::ArrowCursor);

    if (click)
    {
        int hitId = -1;
        double bestDist = 1e18;

        for (auto it = m_targetScenePos.begin(); it != m_targetScenePos.end(); ++it)
        {
            QPoint p = mapFromScene(it.value());
            double d = QLineF(p, event->pos()).length();
            if (d < PICK_RADIUS && d < bestDist)
            {
                bestDist = d;
                hitId = it.key();
            }
        }

        m_selectedTargetId = hitId;   // -1 表示取消选中
        if (m_overlay) m_overlay->setSelectedTarget(m_selectedTargetId);
        updateTargetInfoPanel();
    }

    QGraphicsView::mouseReleaseEvent(event);
}

QVector<int> LXMapGraphicsView::getDir(const QString& path)
{
    QVector<int> vector;
    QDir dir(path);
    dir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);   // 设置过滤类型为文件夹，且不包含隐藏文件夹
    QStringList dirs = dir.entryList();
    for (auto& strDir : dirs)
    {
        bool ok;
        int v = strDir.toInt(&ok);
        if (ok)
        {
            vector.append(v);
        }
    }
    std::sort(vector.begin(), vector.end());
    return vector;
}

/**
 * @brief         传入路径，获取文件夹下所有文件名称，并排序放入数组
 * @param path
 * @return
 */
QVector<int> LXMapGraphicsView::getFile(const QString& path)
{
    QVector<int> vector;
    QDir dir(path);
    dir.setFilter(QDir::Files | QDir::NoDotAndDotDot);   // 设置过滤类型为文件，且不包含隐藏文件
    QStringList files = dir.entryList();
    for (auto& file : files)
    {
        QFileInfo info(file);
        // 将文件名转为数字
        bool ok;
        int v = info.baseName().toInt(&ok);
        if (ok)
        {
            vector.append(v);
        }
    }
    std::sort(vector.begin(), vector.end());
    return vector;
}

/**
 * @brief 加载所有瓦片图
 */
void LXMapGraphicsView::loadImages()
{
    QString root = "./map" + QString("/%1/").arg(17);
    QString format = "jpg";

    m_imageInfos.clear();
    ImageInfo info;
    info.z = 17;
    for (auto& tile : m_tiles)
    {
        QString path = root + QString("%1/%2.%3").arg(tile.x()).arg(tile.y()).arg(format);
        info.url = path;
        info.x = tile.x();
        info.y = tile.y();
        m_imageInfos.append(info);
    }

    m_future = QtConcurrent::map(m_imageInfos, [this](ImageInfo& info) {
        QPixmap pix;
        if (pix.load(info.url))
        {
            info.img = pix;

            // 回到主线程再更新 UI
            QMetaObject::invokeMethod(this, [this, info]() {
                emit updateImage(info);
            }, Qt::QueuedConnection);
        }
    });

}

void LXMapGraphicsView::ensureOverlay()
{
    if (m_overlay)
        return;

    m_overlay = new MapOverlayWidget(this); // parent 会挂到 viewport
    syncOverlayGeometry();
}

void LXMapGraphicsView::syncOverlayGeometry()
{
    if (!m_overlay)
        return;

    // 覆盖层几何必须与 viewport 完全一致
    m_overlay->setGeometry(viewport()->rect());
    m_overlay->raise();

    // 信息框也保证在最上面
    if (m_targetInfoPanel)
        m_targetInfoPanel->raise();
}

QPointF LXMapGraphicsView::calcTargetScenePos(const RadarTargetData& target) const
{
    constexpr int ZOOM = 17;

    double metersPerPixel = Bing::groundResolution(target.centerLatDeg, ZOOM);
    double rangePx = target.rangeMeters / metersPerPixel;

    double rad = qDegreesToRadians(target.azimuthDeg);
    double dx =  rangePx * std::sin(rad);
    double dy = -rangePx * std::cos(rad);

    return QPointF(centerPos.x() + dx, centerPos.y() + dy);
}

void LXMapGraphicsView::updateTargetInfoPanel()
{
    ensureOverlay();

    // 没选中就隐藏
    if (m_selectedTargetId < 0 || !m_radarNewTargets.contains(m_selectedTargetId))
    {
        if (m_targetInfoPanel) m_targetInfoPanel->hide();
        return;
    }

    // 目标点 scene->view
    if (!m_targetScenePos.contains(m_selectedTargetId))
    {
        if (m_targetInfoPanel) m_targetInfoPanel->hide();
        return;
    }

    QPoint viewPos = mapFromScene(m_targetScenePos[m_selectedTargetId]);

    // 视野外：隐藏（你之前提的“像航迹一样看不见”）
    if (!viewport()->rect().contains(viewPos))
    {
        if (m_targetInfoPanel) m_targetInfoPanel->hide();
        return;
    }

    // 懒创建信息框
    if (!m_targetInfoPanel)
    {
        m_targetInfoPanel = new QWidget(m_overlay);
        m_targetInfoPanel->setAttribute(Qt::WA_TranslucentBackground, true);

        // 半透明圆角 + 细边框（你之前说的“半透明圆角矩形”）
        m_targetInfoPanel->setStyleSheet(R"(
            QWidget {
                background: rgba(20, 20, 20, 160);
                border: 1px solid rgba(255, 255, 255, 60);
                border-radius: 12px;
            }
            QLabel {
                color: rgba(230,230,230,220);
                font-size: 14px;
            }
            QLabel#title {
                font-size: 15px;
                font-weight: 600;
                color: rgba(255,255,255,230);
            }
        )");

        auto* layout = new QVBoxLayout(m_targetInfoPanel);
        layout->setContentsMargins(10, 8, 10, 8);
        layout->setSpacing(4);

        QLabel* title = new QLabel("目标信息", m_targetInfoPanel);
        title->setObjectName("title");

        QLabel* lblId   = new QLabel(m_targetInfoPanel); lblId->setObjectName("lblId");
        QLabel* lblAz   = new QLabel(m_targetInfoPanel); lblAz->setObjectName("lblAz");
        QLabel* lblEl   = new QLabel(m_targetInfoPanel); lblEl->setObjectName("lblEl");
        QLabel* lblRng  = new QLabel(m_targetInfoPanel); lblRng->setObjectName("lblRng");

        layout->addWidget(title);
        layout->addWidget(lblId);
        layout->addWidget(lblAz);
        layout->addWidget(lblEl);
        layout->addWidget(lblRng);

        m_targetInfoPanel->setFixedSize(180, 120);
    }

    // 更新文本
    const RadarTargetData& t = m_radarNewTargets[m_selectedTargetId];

    auto setTextByName = [&](const char* objName, const QString& txt){
        if (auto* lb = m_targetInfoPanel->findChild<QLabel*>(objName))
            lb->setText(txt);
    };

    setTextByName("lblId",  QString("ID: %1").arg(t.targetId));
    setTextByName("lblAz",  QString("方位: %1°").arg(t.azimuthDeg, 0, 'f', 1));
    setTextByName("lblEl",  QString("俯仰: %1°").arg(t.elevationDeg, 0, 'f', 1));
    setTextByName("lblRng", QString("距离: %1 m").arg(t.rangeMeters, 0, 'f', 0));

    // 信息框位置：跟随目标点（右上角偏移）
    QPoint panelPos = viewPos + QPoint(12, -m_targetInfoPanel->height() - 12);

    // 防止出界（简单夹紧）
    QRect vr = viewport()->rect();
    panelPos.setX(qBound(vr.left(), panelPos.x(), vr.right() - m_targetInfoPanel->width()));
    panelPos.setY(qBound(vr.top(),  panelPos.y(), vr.bottom() - m_targetInfoPanel->height()));

    m_targetInfoPanel->move(panelPos);
    m_targetInfoPanel->show();
    m_targetInfoPanel->raise();
}

void LXMapGraphicsView::loadOfflineMap(
    int zoomLevel,
    const QString& mapRootPath,
    double centerLon,
    double centerLat
    )
{
    // ---------- 1. 扫描瓦片 ----------
    const QString levelPath =
        mapRootPath + QString("/%1").arg(zoomLevel);

    QVector<int> tileXs = getDir(levelPath);

    m_tiles.clear();
    for (int x : tileXs)
    {
        QString xPath = levelPath + QString("/%1").arg(x);
        QVector<int> tileYs = getFile(xPath);
        for (int y : tileYs)
            m_tiles.append(QPoint(x, y));
    }

    if (m_tiles.isEmpty())
    {
        qWarning() << "No tiles found in" << levelPath;
        return;
    }

    // ---------- 2. 计算 sceneRect（⚠️ 用 min/max，别用 first/last） ----------
    int minX = INT_MAX, minY = INT_MAX;
    int maxX = INT_MIN, maxY = INT_MIN;

    for (const QPoint& t : m_tiles)
    {
        minX = qMin(minX, t.x());
        minY = qMin(minY, t.y());
        maxX = qMax(maxX, t.x());
        maxY = qMax(maxY, t.y());
    }

    QPoint ltTile(minX, minY);
    QPoint rdTile(maxX + 1, maxY + 1);   // +1 才包含完整瓦片

    QPoint ltPx = Bing::tileXYToPixelXY(ltTile);
    QPoint rdPx = Bing::tileXYToPixelXY(rdTile);

    QRect sceneRect(
        ltPx.x(),
        ltPx.y(),
        rdPx.x() - ltPx.x(),
        rdPx.y() - ltPx.y()
        );

    setRect(sceneRect);

    // ---------- 3. 加载瓦片 ----------
    loadImages();   // 你已有逻辑，内部用 m_tiles + zoom

    // ---------- 4. 设置中心点 ----------
    setCenterLonLat(centerLon, centerLat);

    // ---------- 5. 确保 overlay ----------
    ensureOverlay();

    // ---------- 6. 延迟居中（防止 viewport 尚未 ready） ----------
    QTimer::singleShot(0, this, [this]() {
        centerOn(centerPos);
        syncOverlayGeometry();
        if (m_overlay) m_overlay->update();
    });
}

void LXMapGraphicsView::recalcMinScale()
{
    QRectF sr = sceneRect();                // 或 m_scene->sceneRect()
    QSizeF sceneSize = sr.size();
    QSizeF viewSize  = viewport()->size();

    if (sceneSize.isEmpty() || viewSize.isEmpty())
        return;

    // 让地图至少覆盖整个视口：取“填满”的那个比例
    double sx = viewSize.width()  / sceneSize.width();
    double sy = viewSize.height() / sceneSize.height();

    // 你要“不要出现白边”，应该用 max（保证两方向都覆盖）
    m_minScale = qMax(sx, sy);
}

void LXMapGraphicsView::resizeEvent(QResizeEvent* e)
{
    QGraphicsView::resizeEvent(e);
    recalcMinScale();

    syncOverlayGeometry();
    updateTargetInfoPanel();

    // 如果当前缩放比最小还小，强制回到最小
    double s = transform().m11();
    if (s < m_minScale)
    {
        double factor = m_minScale / s;
        scale(factor, factor);
    }
}
