# LXMapGraphicsView 使用说明

## 一、编译与宏定义要求

1. **必须在预处理器中定义宏 `MAPGRAPHICSVIEW_LIBRARY`**

   - 使用该地图库前，请在工程的预处理器宏中加入：

     ```
     MAPGRAPHICSVIEW_LIBRARY
     ```

   - 该宏用于控制类的导出/导入（`__declspec(dllexport / dllimport)`），否则在使用 DLL 或静态库时会出现链接错误。

------

## 二、控件创建方式

1. **创建控件的两种方式**

   - **方式一：代码中直接创建**

     ```
     LXMapGraphicsView* mapView = new LXMapGraphicsView(parent);
     ```

   - **方式二：在 UI 中创建**

     1. 在 Qt Designer 中拖入一个 `QWidget`
     2. 右键 → *提升为 (Promote to)*
     3. 类名填写：`LXMapGraphicsView`
     4. 头文件填写：`LXMapGraphicsView.h`

------

## 三、运行模式要求

1. **必须使用 Release 模式运行**
   - 该地图组件依赖较多图像与缩放计算逻辑
   - Debug 模式下可能存在性能不足、滚动卡顿或瓦片加载不完整的问题
   - **正式使用请统一使用 Release 构建**

------

## 四、地图加载流程

1. **创建对象后，必须主动调用 `loadOfflineMap()`**

   - 创建 `LXMapGraphicsView` 对象后，不会自动加载地图

   - 需要显式调用以下接口完成初始化：

     ```
     mapView->loadOfflineMap(
         zoomLevel,     // 地图层级，例如 17
         "./map",       // 离线瓦片根目录
         centerLon,     // 中心经度
         centerLat      // 中心纬度
     );
     ```

   - 该函数会完成以下工作：

     - 加载离线瓦片
     - 计算并确认地图中心点
     - 设置场景范围
     - 初始化并对齐透明覆盖层（Overlay）

------

## 五、离线地图目录结构要求

1. **必须在正确位置放置 `map` 文件夹**

   - 调试模式（在 IDE 中运行）：

     ```
     map 文件夹应放在 项目工作目录 下
     ```

   - 直接运行 exe（双击运行）：

     ```
     map 文件夹应放在 可执行程序所在目录 下
     ```

   - 示例：

     ```
     MyApp.exe
     map/
     ```

------

## 六、瓦片数据格式要求

1. **正确的瓦片数据目录结构**

   - 地图瓦片必须按照以下格式组织：

     ```
     map\
       └─ 17\
           └─ 瓦片数据
     ```

   - 说明：

     - `17` 为地图层级（Zoom Level）

     - 若使用其他层级（如 16、18），需对应存在：

       ```
       map\16\
       map\18\
       ```