#pragma once
#include "LXMapGraphicsView.h" // 为了复用 RadarTargetData（你也可以把 RadarTargetData 抽到单独头文件）
#include <QWidget>
#include <QMap>
#include <QPointer>
#include <QPainter>
#include <QtMath>
#include <QPushButton>


class LXMapGraphicsView;

// ===== 警戒区编辑模式 =====
enum class AlertEditMode { None, CreateCircle, CreatePolygon };

struct CircleAlertZone {
    QPointF centerScene;
    qreal   radiusScene = 0.0;   // scene单位（像素）
};

struct PolygonAlertZone {
    QVector<QPointF> pointsScene;
};


class MAPGRAPHICSVIEW_EXPORT MapOverlayWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MapOverlayWidget(LXMapGraphicsView* view);

    void setTargets(const QMap<int, RadarTargetData>& targets);
    void setTargetScenePos(int targetId, const QPointF& scenePos);
    void setSelectedTarget(int targetId);

    // ✅ 新增：追加航迹点（内部自动限长）
    void appendTrackPoint(int id, const QPointF& scenePos);

    bool hasTarget(int targetId) const { return m_targets.contains(targetId); }
    QPoint viewPosOf(int targetId) const;          // scene -> view
    bool isTargetInView(int targetId) const;       // 是否在 viewport 视野内

    // ===== 雷达范围线参数 =====
public:
    void setRadarParams(const QPointF& centerScene,
                        double centerLatDeg,
                        const QVector<double>& ringMeters,
                        double crossArmMeters);

    void checkAlertZones(int targetId, const QPointF& targetScenePos);

    void initAlertButtons();
    void layoutAlertButtons(); // 处理 resize 时保持位置
signals:
    void sgnAlertTriggered(int targetId);   // 你也可以用 batchId/targetId

public slots:
    void startCreateCircleZone();
    void startCreatePolygonZone();
    void clearAlertZones();
    void stopAlertEdit(); // 可选：外部强制退出编辑

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

protected:
    bool event(QEvent* e) override;

private:
    bool forwardToViewport(QEvent* e);
    bool hitOnButtons(const QPoint& pos) const;

private:
    QPointer<LXMapGraphicsView> m_view;
    QMap<int, RadarTargetData> m_targets;          // 最新目标数据（用于显示数值）
    QMap<int, QPointF> m_targetScenePos;           // 最新目标点（scene 像素坐标）

    // 航迹（scene 点序列）
    QMap<int, QVector<QPointF>> m_tracks;

    constexpr static double TARGET_SIZE = 10.0;
    int m_selectedId = -1;
    int m_maxTrackPoints = 60;  // 你想更长就调大（比如 100/200）

private:
    bool m_hasRadar = false;
    QPointF m_radarCenterScene;
    double m_radarCenterLatDeg = 0.0;
    QVector<double> m_ringMeters;
    double m_crossArmMeters = 0.0;

    int m_majorRingStepMeters = 600;  // 主圈步进（粗）
    int m_minorRingStepMeters = 300;  // 辅圈步进（细）

    AlertEditMode m_alertMode = AlertEditMode::None;

    QVector<CircleAlertZone>  m_circleZones;
    QVector<PolygonAlertZone> m_polygonZones;

    // 圆形创建过程
    bool   m_circleCenterSet = false;
    QPointF m_circleCenterScene;
    qreal  m_circleRadiusScene = 0.0;

    // 多边形创建过程
    QVector<QPointF> m_polygonTempScenePoints;

    // 已经触发报警的目标（避免重复报警）
    QSet<int> m_alarmTargets;

    QPushButton* m_btnCircle  = nullptr;
    QPushButton* m_btnPolygon = nullptr;
    QPushButton* m_btnClear   = nullptr;

};
