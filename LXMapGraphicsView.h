#ifndef MAPGRAPHICSVIEW_H
#define MAPGRAPHICSVIEW_H
#include "mapgraphicsview_global.h"
#include "mapStruct.h"
#include <QGraphicsView>
#include <QVector>
#include <QMap>
#include <QFuture>
class MapOverlayWidget;
struct RadarTargetData
{
    int targetId        = -1;    // 目标ID
    double azimuthDeg   = 0.0;   // 方位角
    double elevationDeg = 0.0;   // 俯仰角
    double rangeMeters  = 0.0;   // 距离（米）
    double centerLatDeg = 0.0;   // 中心点纬度（用于米→像素转换）

    RadarTargetData() = default;

    RadarTargetData(int id, double az, double el, double range, double lat)
        : targetId(id), azimuthDeg(az), elevationDeg(el), rangeMeters(range), centerLatDeg(lat) {}
};

class MAPGRAPHICSVIEW_EXPORT LXMapGraphicsView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit LXMapGraphicsView(QWidget* parent = nullptr);
    ~LXMapGraphicsView() override;

    void setRect(QRect rect);
    void drawImg(const ImageInfo& info);
    void clear();

    // 设置中心点（scene 像素坐标）
    void setCenterScene(const QPointF& scenePos);

    // 设置中心点（经纬度，固定 17 级）
    void setCenterLonLat(double lon, double lat);

    void drawRadarCircle(const QPointF& centerPixel, double radiusMeters, double centerLatDeg);
    void drawCenterCross(const QPointF& centerPixel, double armLengthMeters, double centerLatDeg);

    void loadOfflineMap(
        int zoomLevel,
        const QString& mapRootPath,
        double centerLon,
        double centerLat
        );

    // 雷达目标显示（方位-距离）
    void drawRadarTarget(RadarTargetData traget);
    void recalcMinScale();
    QVector<int> getDir(const QString& path);
    QVector<int> getFile(const QString& path);
    void loadImages();


signals:
    void updateImage(const ImageInfo& info);   // 添加瓦片图
    void zoom(bool flag);                      // 缩放 true：放大
    void showRect(QRect rect);
    void mousePos(QPoint pos);

    void sgnTargetGuide(double az,double pitch);

protected:
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    void getShowRect();   // 获取显示范围

private:
    QGraphicsScene* m_scene = nullptr;
    QPointF m_pos;
    QPointF m_scenePos;

    QPointF centerPos;

    QMap<int, RadarTargetData> m_radarNewTargets;


private:

	enum {
		PICK_RADIUS = 30,
		DRAG_THRESHOLD = 6
	};

    double m_minScale = 0.1;
    double m_maxScale = 3.0;

    // 当前选中的目标 ID
    int m_selectedTargetId = -1;

    QWidget* m_targetInfoPanel = nullptr;  // 当前选中目标信息面板

    bool   m_leftPressed = false;
    bool   m_isDragging  = false;
    QPoint m_pressPos;           // view 坐标
    QPoint m_lastPos;

    QList<QPoint> m_tiles;
    QList<ImageInfo> m_imageInfos;   // 瓦片地图信息
    QFuture<void> m_future;

    // private 区域新增：
private:
    QPointF calcTargetScenePos(const RadarTargetData& target) const;
    void ensureOverlay();
    void syncOverlayGeometry();
    void updateTargetInfoPanel();   // 根据 m_selectedTargetId 更新信息框位置/显示

private:
    MapOverlayWidget* m_overlay = nullptr;

    // 最新目标点（scene 坐标），只保留“最新点”，不再往 scene 里 addEllipse
    QMap<int, QPointF> m_targetScenePos;

};

#endif   // MAPGRAPHICSVIEW_H
