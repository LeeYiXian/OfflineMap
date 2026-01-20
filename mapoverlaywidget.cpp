#include "mapoverlaywidget.h"
#include "LXMapGraphicsView.h"

#include "bingformula.h"
#include <QMouseEvent>
#include <QCoreApplication>

MapOverlayWidget::MapOverlayWidget(LXMapGraphicsView* view)
    : QWidget(view ? view->viewport() : nullptr)
    , m_view(view)
{
    // 覆盖层必须透明背景
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setMouseTracking(true);

    initAlertButtons();   // 加这一行

    // 始终盖在 viewport 上
    raise();
    show();
}

void MapOverlayWidget::initAlertButtons()
{
    // ===== 雷达面板内按钮 =====
	m_btnCircle = new QPushButton(QString::fromUtf8(u8"圆形警戒区"), this);
	m_btnPolygon = new QPushButton(QString::fromUtf8(u8"多边形警戒区"), this);
	m_btnClear = new QPushButton(QString::fromUtf8(u8"清除警戒区"), this);


    // 样式（原封不动）
    QString style = R"(
        QPushButton {
            background-color: rgba(30,30,30,180);
            color: white;
            border: 1px solid #4caf50;
            border-radius: 6px;
            padding: 6px 12px;
        }
        QPushButton:hover {
            background-color: rgba(76,175,80,200);
        }
    )";

    m_btnCircle->setStyleSheet(style);
    m_btnPolygon->setStyleSheet(style);
    m_btnClear->setStyleSheet(style);

    // 固定大小（关键：不会被缩放）
    m_btnCircle->setFixedSize(120, 32);
    m_btnPolygon->setFixedSize(120, 32);
    m_btnClear->setFixedSize(120, 32);

    // 位置（相对 overlay 左上角）
    m_btnCircle->move(20, 20);
    m_btnPolygon->move(20, 60);
    m_btnClear->move(20, 100);

    // 确保在最上层
    m_btnCircle->raise();
    m_btnPolygon->raise();
    m_btnClear->raise();

    // 非常重要：按钮要能点（否则 overlay 透明给鼠标时点不到）
    // 给按钮单独设置：不透明给鼠标事件
    m_btnCircle->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_btnPolygon->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_btnClear->setAttribute(Qt::WA_TransparentForMouseEvents, false);

    // 信号连接（连接到 overlay 自己的槽）
    connect(m_btnCircle,  &QPushButton::clicked, this, &MapOverlayWidget::startCreateCircleZone);
    connect(m_btnPolygon, &QPushButton::clicked, this, &MapOverlayWidget::startCreatePolygonZone);
    connect(m_btnClear,   &QPushButton::clicked, this, &MapOverlayWidget::clearAlertZones);
}

void MapOverlayWidget::appendTrackPoint(int id, const QPointF& scenePos)
{
    auto& vec = m_tracks[id];

    // 去抖：如果点几乎没动，就不重复塞（避免线段抖成一团）
    if (!vec.isEmpty())
    {
        const QPointF& last = vec.last();
        if (QLineF(last, scenePos).length() < 1.0)  // 1px 阈值，可调
            return;
    }

    vec.push_back(scenePos);

    // 限长
    if (vec.size() > m_maxTrackPoints)
        vec.erase(vec.begin(), vec.begin() + (vec.size() - m_maxTrackPoints));

    update();
}

void MapOverlayWidget::setTargets(const QMap<int, RadarTargetData>& targets)
{
    m_targets = targets;
    update();
}

void MapOverlayWidget::setTargetScenePos(int targetId, const QPointF& scenePos)
{
    m_targetScenePos[targetId] = scenePos;
    update();
}

void MapOverlayWidget::setSelectedTarget(int id)
{
    m_selectedId = id;
    update();
}

QPoint MapOverlayWidget::viewPosOf(int targetId) const
{
    if (!m_view || !m_targetScenePos.contains(targetId))
        return QPoint(-999999, -999999);

    return m_view->mapFromScene(m_targetScenePos.value(targetId));
}

bool MapOverlayWidget::isTargetInView(int targetId) const
{
    QPoint p = viewPosOf(targetId);
    return rect().contains(p);
}

void MapOverlayWidget::paintEvent(QPaintEvent* e)
{
    Q_UNUSED(e)

    if (!m_view)
        return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setClipRect(rect());

    // ========= 雷达HUD（统一虚线风格） =========
    if (m_hasRadar)
    {
        const QPointF centerView = m_view->mapFromScene(m_radarCenterScene);

        // 米 -> scene像素
        constexpr int ZOOM = 17;
        const double metersPerPixel =
            Bing::groundResolution(m_radarCenterLatDeg, ZOOM);

        // scene像素 -> view像素
        const double s = m_view->transform().m11();
        auto metersToViewPx = [&](double meters) -> double {
            return (meters / metersPerPixel) * s;
        };

        // ===== 雷达绿配色（只靠透明度区分） =====
        const QColor greenMajor(0, 255, 120, 200);
        const QColor greenMid  (0, 255, 120, 150);
        const QColor greenMinor(0, 255, 120, 100);
        const QColor greenFaint(0, 255, 120, 70);

        p.setBrush(Qt::NoBrush);

        // ===== 同心圆（全虚线）=====
        for (double meters : m_ringMeters)
        {
            const double r = metersToViewPx(meters);
            const bool isMajor =
                (m_majorRingStepMeters > 0) &&
                (qRound(meters) % m_majorRingStepMeters == 0);

            QPen pen;
            if (isMajor)
            {
                pen = QPen(greenMid, 1.8);
                pen.setDashPattern({6, 6});
            }
            else
            {
                pen = QPen(greenMinor, 1.2);
                pen.setDashPattern({4, 6});
            }

            pen.setCapStyle(Qt::RoundCap);
            p.setPen(pen);

            QRectF rc(centerView.x() - r, centerView.y() - r, r * 2, r * 2);
            p.drawEllipse(rc);
        }

        // ===== 主十字线（虚线，最强）=====
        const double arm = metersToViewPx(m_crossArmMeters);
        {
            QPen pen(greenMajor, 2.4);
            pen.setDashPattern({8, 6});
            pen.setCapStyle(Qt::RoundCap);
            p.setPen(pen);

            p.drawLine(QPointF(centerView.x() - arm, centerView.y()),
                       QPointF(centerView.x() + arm, centerView.y()));
            p.drawLine(QPointF(centerView.x(), centerView.y() - arm),
                       QPointF(centerView.x(), centerView.y() + arm));
        }

        // ===== 米字线（45°，虚线，中等）=====
        {
            QPen pen(greenMid, 1.6);
            pen.setDashPattern({6, 6});
            pen.setCapStyle(Qt::RoundCap);
            p.setPen(pen);

            const double d = arm / std::sqrt(2.0);

            p.drawLine(QPointF(centerView.x() - d, centerView.y() - d),
                       QPointF(centerView.x() + d, centerView.y() + d));
            p.drawLine(QPointF(centerView.x() - d, centerView.y() + d),
                       QPointF(centerView.x() + d, centerView.y() - d));
        }

        // ===== 中心弱点（可选）=====
        {
            p.setPen(Qt::NoPen);
            p.setBrush(greenFaint);
            p.drawEllipse(centerView, 3.0, 3.0);
        }

        // ===== 每个同心圆只显示一个距离值：统一在水平线右侧 =====
        {
            // 文本样式（清晰但不抢画面）
            QFont f = p.font();
            f.setPixelSize(13);
            f.setBold(true);
            p.setFont(f);

            const QColor textColor(0, 255, 120, 200);
            QPen textPen(textColor);
            p.setPen(textPen);

            QFontMetrics fm(p.font());

            // 轻微背景块，让字在地图上更清晰（不想要可删掉背景那几行）
            auto drawTag = [&](const QPointF& anchor, const QString& txt)
            {
                const int w = fm.horizontalAdvance(txt);
                const int h = fm.height();

                QRectF box(anchor.x(),
                           anchor.y() - h + 4,
                           w + 10,
                           h + 6);

                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0, 0, 0, 90));
                p.drawRoundedRect(box, 6, 6);

                p.setPen(textPen);
                p.drawText(box.adjusted(6, 0, -4, 0),
                           Qt::AlignVCenter | Qt::AlignLeft,
                           txt);
            };

            // 逐圈标注：每个圈只标一个值（右侧）
            for (int i = 0; i < m_ringMeters.size(); ++i)
            {
                const double meters = m_ringMeters[i];
                const double r = metersToViewPx(meters);

                // 文字内容
                const QString txt = QString::number(int(qRound(meters))) + "m";

                // 位置：水平线右侧，稍微上移避免压线
                // 如果你觉得文字太靠近线，可以把 -16 改成 -20 或 -24
                QPointF pos(centerView.x() + r + 8.0, centerView.y() - 16.0);

                drawTag(pos, txt);
            }
        }
    }

    // ========= 警戒区（scene坐标 -> view坐标绘制） =========
    {
        // 圆：绿
        p.setPen(QPen(QColor(0, 255, 0, 200), 2));
        p.setBrush(QColor(0, 255, 0, 60));

        for (const auto& c : m_circleZones)
        {
            QPointF centerView = m_view->mapFromScene(c.centerScene);
            const double s = m_view->transform().m11();
            const double rView = c.radiusScene * s;

            p.drawEllipse(centerView, rView, rView);
        }

        // 圆预览（虚线）
        if (m_alertMode == AlertEditMode::CreateCircle && m_circleCenterSet)
        {
            p.setPen(QPen(Qt::green, 2, Qt::DashLine));
            p.setBrush(QColor(0, 255, 0, 40));

            QPointF centerView = m_view->mapFromScene(m_circleCenterScene);
            double s = m_view->transform().m11();
            double rView = m_circleRadiusScene * s;

            p.drawEllipse(centerView, rView, rView);
        }

        // 多边形：红
        p.setPen(QPen(QColor(255, 0, 0, 220), 2));
        p.setBrush(QColor(255, 0, 0, 60));

        for (const auto& poly : m_polygonZones)
        {
            QPolygonF viewPoly;
            viewPoly.reserve(poly.pointsScene.size());
            for (const auto& sp : poly.pointsScene)
                viewPoly << m_view->mapFromScene(sp);

            p.drawPolygon(viewPoly);
        }

        // 多边形预览（虚线折线）
        if (m_alertMode == AlertEditMode::CreatePolygon && !m_polygonTempScenePoints.isEmpty())
        {
            p.setPen(QPen(Qt::red, 2, Qt::DashLine));
            p.setBrush(Qt::NoBrush);

            QPolygonF viewLine;
            viewLine.reserve(m_polygonTempScenePoints.size());
            for (const auto& sp : m_polygonTempScenePoints)
                viewLine << m_view->mapFromScene(sp);

            p.drawPolyline(viewLine);
        }
    }

    // ========= 1) 画航迹折线 =========
    for (auto it = m_tracks.begin(); it != m_tracks.end(); ++it)
    {
        const int id = it.key();
        const QVector<QPointF>& scenePts = it.value();
        if (scenePts.size() < 2)
            continue;

        QPolygon poly;
        poly.reserve(scenePts.size());
        for (const QPointF& sp : scenePts)
            poly << m_view->mapFromScene(sp);

        // 选中目标更粗更亮
        QPen pen;
        if (id == m_selectedId)
        {
            pen = QPen(QColor(255, 255, 0, 220), 2.5);  // 黄
        }
        else
        {
            pen = QPen(QColor(0, 255, 0, 160), 1.8);    // 绿
        }
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);

        p.drawPolyline(poly);
    }

    // ========= 2) 画最新点（圆点） =========
    for (auto it = m_targetScenePos.begin(); it != m_targetScenePos.end(); ++it)
    {
        const int id = it.key();
        const QPoint viewPos = m_view->mapFromScene(it.value());

        // 视野外不画
        if (!rect().contains(viewPos))
            continue;

        const bool selected = (id == m_selectedId);

        QPen pen(selected ? QColor(255,255,0,240) : QColor(0,255,0,200));
        pen.setWidthF(selected ? 2.2 : 1.8);
        p.setPen(pen);

        p.setBrush(selected ? QColor(255,255,0,180) : QColor(0,255,0,120));

        const double r = selected ? 6.0 : 5.0;
        p.drawEllipse(QPointF(viewPos), r, r);

        // 可选：画 ID
        // p.setPen(QColor(255,255,255,200));
        // p.drawText(viewPos + QPoint(8, -8), QString::number(id));
    }
}

void MapOverlayWidget::setRadarParams(const QPointF& centerScene,
                                      double centerLatDeg,
                                      const QVector<double>& ringMeters,
                                      double crossArmMeters)
{
    m_hasRadar = true;
    m_radarCenterScene = centerScene;
    m_radarCenterLatDeg = centerLatDeg;
    m_ringMeters = ringMeters;
    m_crossArmMeters = crossArmMeters;
}

void MapOverlayWidget::startCreateCircleZone()
{
    m_alertMode = AlertEditMode::CreateCircle;
    m_circleCenterSet = false;
    m_circleRadiusScene = 0.0;
    setCursor(Qt::CrossCursor);

    update();
}

void MapOverlayWidget::startCreatePolygonZone()
{
    m_alertMode = AlertEditMode::CreatePolygon;
    m_polygonTempScenePoints.clear();
    setCursor(Qt::CrossCursor);

    update();
}

void MapOverlayWidget::stopAlertEdit()
{
    m_alertMode = AlertEditMode::None;
    m_circleCenterSet = false;
    m_circleRadiusScene = 0.0;
    m_polygonTempScenePoints.clear();
    unsetCursor();

    update();
}

void MapOverlayWidget::clearAlertZones()
{
    m_circleZones.clear();
    m_polygonZones.clear();
    m_polygonTempScenePoints.clear();
    m_alarmTargets.clear();
    stopAlertEdit();
}

void MapOverlayWidget::mousePressEvent(QMouseEvent* e)
{
    if (!m_view) { e->ignore(); return; }

    const QPointF scenePos = m_view->mapToScene(e->pos());

    // ===== 圆形 =====
    if (m_alertMode == AlertEditMode::CreateCircle)
    {
        if (e->button() == Qt::LeftButton)
        {
            if (!m_circleCenterSet)
            {
                m_circleCenterScene = scenePos;
                m_circleCenterSet = true;
            }
            e->accept();
            return;
        }
        if (e->button() == Qt::RightButton)
        {
            stopAlertEdit();
            e->accept();
            return;
        }
    }

    // ===== 多边形 =====
    if (m_alertMode == AlertEditMode::CreatePolygon)
    {
        if (e->button() == Qt::LeftButton)
        {
            m_polygonTempScenePoints.append(scenePos);
            update();
            e->accept();
            return;
        }
        if (e->button() == Qt::RightButton)
        {
            if (m_polygonTempScenePoints.size() >= 3)
            {
                PolygonAlertZone zone;
                zone.pointsScene = m_polygonTempScenePoints;
                m_polygonZones.append(zone);
            }
            m_polygonTempScenePoints.clear();
            stopAlertEdit();
            e->accept();
            return;
        }
    }

    // 非编辑模式不处理
    e->ignore();
}

void MapOverlayWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_view) { e->ignore(); return; }

    if (m_alertMode == AlertEditMode::CreateCircle && m_circleCenterSet)
    {
        const QPointF scenePos = m_view->mapToScene(e->pos());
        m_circleRadiusScene = QLineF(m_circleCenterScene, scenePos).length();
        update();
        e->accept();
        return;
    }

    e->ignore();
}

void MapOverlayWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (m_alertMode == AlertEditMode::CreateCircle &&
        e->button() == Qt::LeftButton &&
        m_circleCenterSet)
    {
        if (m_circleRadiusScene > 5.0)
        {
            CircleAlertZone zone;
            zone.centerScene = m_circleCenterScene;
            zone.radiusScene = m_circleRadiusScene;
            m_circleZones.append(zone);
        }

        m_circleCenterSet = false;
        m_circleRadiusScene = 0.0;
        stopAlertEdit();

        e->accept();
        return;
    }

    e->ignore();
}

void MapOverlayWidget::checkAlertZones(int targetId, const QPointF& targetScenePos)
{
    bool inAnyZone = false;

    // 1) 圆
    for (const auto& c : m_circleZones)
    {
        if (QLineF(targetScenePos, c.centerScene).length() <= c.radiusScene)
        {
            inAnyZone = true;
            break;
        }
    }

    // 2) 多边形（只有当圆没命中时再算，省一点）
    if (!inAnyZone)
    {
        for (const auto& poly : m_polygonZones)
        {
            QPolygonF polygon(poly.pointsScene);
            if (polygon.containsPoint(targetScenePos, Qt::OddEvenFill))
            {
                inAnyZone = true;
                break;
            }
        }
    }

    // ===== 状态机：进入/离开 =====
    const bool wasIn = m_alarmTargets.contains(targetId);

    if (inAnyZone && !wasIn)
    {
        m_alarmTargets.insert(targetId);
        emit sgnAlertTriggered(targetId);   // 进入触发
    }
    else if (!inAnyZone && wasIn)
    {
        m_alarmTargets.remove(targetId);    // 离开移除（你要的）
        // 如果你需要“离开事件”，这里可以加一个 signal
        // emit sgnAlertCleared(targetId);
    }
}


bool MapOverlayWidget::hitOnButtons(const QPoint& pos) const
{
    auto hit = [&](QWidget* w)->bool {
        return w && w->isVisible() && w->geometry().contains(pos);
    };
    return hit(m_btnCircle) || hit(m_btnPolygon) || hit(m_btnClear);
}

bool MapOverlayWidget::forwardToViewport(QEvent* e)
{
    if (!m_view || !m_view->viewport())
        return false;

    // 把事件“投递”到 viewport（坐标系一样：overlay 就挂在 viewport 上）
    QCoreApplication::sendEvent(m_view->viewport(), e);
    return true;
}

bool MapOverlayWidget::event(QEvent* e)
{
    // 只转发鼠标相关事件，滚轮不需要（滚轮本来就发给 view）
    switch (e->type())
    {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseMove:
    case QEvent::MouseButtonDblClick:
    {
        auto* me = static_cast<QMouseEvent*>(e);

        // 1) 如果点在按钮上：让 QWidget 默认机制处理（按钮能点）
        if (hitOnButtons(me->pos()))
            return QWidget::event(e);

        // 2) 如果你在“警戒区编辑模式”：由 overlay 自己处理（画圆/画多边形）
        if (m_alertMode != AlertEditMode::None)
            return QWidget::event(e);

        // 3) 其他情况：转发给 viewport（地图继续负责拖拽/点击）
        return forwardToViewport(e);
    }
    default:
        break;
    }

    return QWidget::event(e);
}
