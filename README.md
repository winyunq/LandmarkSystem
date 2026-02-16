# LandmarkSystem for UE5

**LandmarkSystem** is a high-performance, LOD-aware map labeling system designed for RTS and Grand Strategy games in Unreal Engine 5. It solves the specific challenge of displaying readable map labels (Cities, Rivers, Mountains) across massive zoom ranges, implementing "Counter-intuitive Scaling" effectively.

## How to Use

### 1. Editing Landmarks (Editor)

1.  **Place Actor**: Drag and drop a **`LandmarkCloudActor`** into your level.
2.  **Configure**: In the Details panel, find the **"Landmark IO"** category.
    *   Set **`JsonFileName`** to a unique name for this level, e.g., `Landmarks_LevelName.json`.
    *   *Note: Files are saved to `Content/MapData/`.*
3.  **Edit Points**:
    *   Select the **`LandmarkCloudComponent`** (or just click the Actor).
    *   **Add Points**: Click the **`Add Landmark Point`** button in the Details panel. (This adds a point at the Actor's location).
    *   **Move Points**: Drag the diamond handles in the viewport.
    *   **Properties**: Expand the `Landmarks` array to edit names ("DisplayName") or ID.
4.  **Load/Save**:
    *   Click **`Load From Json`** to load existing data from disk (if any).
    *   Click **`Save To Json`** to save your changes to the file.

### 2. Runtime (Game)

*   **No Setup Required**: You do **not** need the `LandmarkCloudActor` at runtime. It destroys itself automatically.
*   **Auto-Load**: The `LandmarkSubsystem` automatically looks for `Content/MapData/Landmarks_<MapName>.json` when the world begins.
*   **Verification**: Check the Output Log for "LandmarkSubsystem: Successfully loaded..." messages.

## 核心特性 (Core Features)

### 1. 反直觉缩放 (Counter-intuitive / Adaptive Scaling)
在传统透视投影中，当相机拉远时，物体会变小直到不可见。而在策略地图中，我们希望：
*   **近景 (Micro)**: 标签显示正常大小，或者隐藏（以免遮挡单位）。
*   **远景 (Macro)**: 标签保持可读大小，甚至相对变得“更大”，成为主要的地标指示。

本系统通过 `ULandmarkSubsystem` 动态计算 `ZoomFactor` (0.0 - 1.0)，并通过可配置的 `RuntimeFloatCurve` 驱动标签的 **Scale** 和 **Opacity**。

### 2. 混合数据源架构 (Hybrid Data Sources)
系统采用**聚合器模式**，`ULandmarkSubsystem` 作为中心，接受来自不同来源的注册：

*   **静态源 (Static)**: `ALandmarkMapLabelProxy`
    *   **原理**: 编辑器专用 Actor (`IsEditorOnlyActor=true`)。
    *   **用途**: 摆放山脉、平原等永久地名。
    *   **性能**: 打包时自动剔除，无运行时 Actor 开销。数据在构建时或加载时注册到 Subsystem。
*   **动态源 (Dynamic)**: Actor API
    *   **原理**: 任何 Actor (如 `AORTSCityActor`) 可在 `BeginPlay` 调用 `RegisterLandmark`。
    *   **用途**: 城市、移动的军队、任务点。支持运行时更新位置和名字。
*   **过程化源 (Procedural)**: `ALandmarkPathGenerator`
    *   **原理**: 基于 Spline 组件。
    *   **用途**: 河流、商路、国界线。
    *   **算法**: 沿样条线每隔一定距离 (Spacing) 自动插值生成一个地标点。

### 3. 高性能渲染 (High Performance Rendering)
*   **视锥剔除 (Frustum Culling)**: 每帧计算，仅处理屏幕内的地标。
*   **Canvas 批处理**: 避免为每个标签创建庞大的 UMG Widget。直接在 `HUD::DrawHUD` 中利用 `Canvas->DrawText` 进行极速绘制，轻松支持同屏数百个标签。

## 技术架构 (Architecture)

### 类结构
*   `ULandmarkSubsystem` (UWorldSubsystem): 核心管理器。维护 `TMap<ID, Data>`。
*   `FLandmarkInstanceData`: 扁平化数据结构 (P.O.D.)，包含位置、名字、视觉配置。
*   `FLandmarkVisualConfig`: 定义显示的 Zoom 范围 (`MinVisibleZoom`, `MaxVisibleZoom`) 和优先级。

### 关键流程
1.  **注册**: 数据源调用 `Subsystem->RegisterLandmark(Data)`。
2.  **更新**: 相机 (PlayerController) 在且仅在位置变化时调用 `Subsystem->UpdateCameraState(Location, Zoom)`。
3.  **计算**: Subsystem 遍历数据 -> 剔除 -> 计算屏幕坐标 -> 计算缩放/透明度 -> 缓存结果。
4.  **渲染**: HUD 调用 `Subsystem->GetVisibleLandmarks()` -> `Canvas->DrawText`。

## 使用方法 (Usage)

### 1. 摆放静态地标
在编辑器中拖入 `LandmarkMapLabelProxy`，设置 `DisplayName` 和 `VisualConfig`。

### 2. 代码注册动态地标
```cpp
// 在你的 Actor (如 City) 中
void AMyCity::BeginPlay()
{
    Super::BeginPlay();
    if (auto* Sys = GetWorld()->GetSubsystem<ULandmarkSubsystem>())
    {
        FLandmarkInstanceData Data;
        Data.ID = GetName();
        Data.DisplayName = FText::FromString("Chang'an");
        Data.WorldLocation = GetActorLocation();
        Data.VisualConfig.BaseScale = 1.5f;
        // 关键：设置在什么缩放级别可见
        Data.VisualConfig.MinVisibleZoom = 0.0f; // 地面
        Data.VisualConfig.MaxVisibleZoom = 1.0f; // 太空
        
        Sys->RegisterLandmark(Data);
    }
}
```

### 3. 设置过程化河流
拖入 `LandmarkPathGenerator`，编辑 Spline 路径，设置 `BaseDisplayName` 为 "Yellow River"，设置 `Spacing` 为 5000。

## License
MIT License. See LICENSE file.
