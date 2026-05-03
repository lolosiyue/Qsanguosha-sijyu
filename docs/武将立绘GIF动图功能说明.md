# 武将立绘 GIF 动图功能说明

## 功能概述

本功能支持在游戏内武将立绘显示 GIF 格式的动图。玩家可以在配置界面选择是否开启此功能。

### 目录结构

```
image/
├── fullskin/
│   └── generals/
│       └── full/
│           ├── 关羽.jpg              (静态图)
│           └── gif/                 (GIF动图目录)
│               ├── 关羽.gif
│               └── 张飞.gif
│
└── heroskin/
    └── fullskin/
        └── generals/
            └── full/
                ├── 关羽_1.jpg         (heroskin静态图)
                ├── 关羽_2.jpg
                └── gif/               (heroskin GIF动图目录)
                    ├── 关羽_1.gif
                    ├── 关羽_2.gif
                    └── 张飞_1.gif
```

### GIF 文件命名规则

| 场景 | 文件命名 | 放置路径 |
|------|---------|---------|
| 无 heroskin | `关羽.gif` | `image/fullskin/generals/full/gif/关羽.gif` |
| heroskin #1 | `关羽_1.gif` | `image/heroskin/fullskin/generals/full/gif/关羽_1.gif` |
| heroskin #2 | `关羽_2.gif` | `image/heroskin/fullskin/generals/full/gif/关羽_2.gif` |

---

## 使用方式

### 1. 基础使用（自动查找）

将 GIF 文件按照上述目录结构放置后，系统会自动查找并播放：

1. **优先查找 `/gif/` 子目录**
   - `image/fullskin/generals/full/关羽.jpg` → `image/fullskin/generals/full/gif/关羽.gif`
   - `image/heroskin/.../关羽_1.jpg` → `image/heroskin/.../gif/关羽_1.gif`

2. **回退到同目录**
   - 如果 `/gif/` 子目录不存在，则查找同目录下同名 `.gif` 文件

### 2. 资源别名（可选）

对于需要特殊处理或跨目录复用的 GIF，可以使用资源别名系统注册：

```cpp
// 在初始化代码中注册
Sanguosha->addResourceAlias("animatedgeneral", "关羽", "关羽_动画版.gif");

// 支持完整路径
Sanguosha->addResourceAlias("animatedgeneral", "关羽", "image/special/关羽_animated.gif");

// heroskin 专用别名
Sanguosha->addResourceAlias("animatedgeneral", "关羽_1", "关羽_1_heroskin.gif");
```

别名查找优先级：
```
1. 检查 animatedgeneral 资源别名
2. 查找 /full/gif/ 子目录
3. 查找同目录 .gif 文件
```

---

## 配置选项

### 配置界面

在游戏设置对话框（`ConfigDialog`）的 **Environment** 选项卡中：

- **Enable animated generals (GIF)** - 勾选以启用武将立绘 GIF 动图功能

### 配置键值

| 键值 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `EnableAnimatedGenerals` | bool | `true` | 是否启用 GIF 动图 |

---

## 技术实现

### 核心文件

| 文件 | 功能 |
|------|------|
| `src/ui/graphicspixmaphoveritem.h/cpp` | GIF 加载、显示和控制 |
| `src/ui/generic-cardcontainer-ui.cpp` | 头像更新时调用 GIF 设置 |
| `src/dialog/configdialog.ui/cpp` | 配置界面复选框 |

### 主要类和方法

#### GraphicsPixmapHoverItem

```cpp
// 设置武将图片（支持GIF）
void setGeneralImage(const QString &imagePath, const QSize &targetSize);

// 直接设置pixmap（无GIF支持）
void setGeneralImage(const QPixmap &pixmap, const QSize &targetSize);

// GIF 动画控制
void stopGifAnimation();
void startGifAnimation();
bool isAnimated() const;
```

### 显示机制

- 使用 `QMovie` 加载和播放 GIF
- 通过 `QGraphicsProxyWidget` 将 `QLabel`（承载 `QMovie`）嵌入 `QGraphicsScene`
- 设置 `zValue = -1` 确保 GIF 在静态层下方，不遮挡皮肤切换特效

### 皮肤切换兼容性

当玩家切换武将皮肤时：
1. 当前 GIF 动画暂停并隐藏
2. 显示静态图用于皮肤切换特效
3. 切换完成后重新加载并显示 GIF

---

## 注意事项

1. **性能考虑**：GIF 会占用更多内存，建议在低配置机器上关闭此功能

2. **目录创建**：使用此功能前需在对应目录创建 `gif/` 子文件夹

3. **文件格式**：仅支持 `.gif` 格式，不支持其他动画格式

4. **回退机制**：当 GIF 文件不存在或加载失败时，自动回退到静态图显示

---

## 相关文档

- [动态皮肤功能使用文档](dynamic-skin-guide.md) - Spine 动画系统说明
- [场景切换与语音动画移植说明](場景切換與語音動畫移植說明.md)
