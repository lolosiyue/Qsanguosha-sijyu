# 武将立绘 GIF 动图功能说明

## 功能概述

本功能支持在游戏内武将立绘显示 GIF 格式的动图。玩家可以在配置界面选择是否开启此功能。

### 目录结构

```
image/
└── fullskin/
    └── generals/
        └── full/
            ├── 关羽.jpg              (预设皮肤静态图, skinIndex = 0)
            └── gif/                 (预设皮肤 GIF 动图目录, 优先查找)
                ├── 关羽.gif
                └── 张飞.gif

hero-skin/                             (heroskin 皮肤目录, skinIndex > 0)
└── 关羽/
    └── 1/                             (皮肤编号)
        ├── full.png                  (皮肤静态图, 不存在时回退 card.jpg)
        ├── full.gif                  (皮肤 GIF 动图, 与静态图同目录)
        └── card.jpg                  (皮肤卡片图)
```

### GIF 文件查找规则

| 场景 | 静态图路径 | GIF 路径 |
|------|-----------|---------|
| 预设皮肤 (skinIndex = 0) | `image/fullskin/generals/full/关羽.jpg` | `image/fullskin/generals/full/gif/关羽.gif`（子目录优先）；子目录不存在则回退同目录 `关羽.gif` |
| heroskin #1 | `hero-skin/关羽/1/full.png` | `hero-skin/关羽/1/full.gif`（仅同目录，无 `/gif/` 子目录） |
| heroskin #2 | `hero-skin/关羽/2/full.png` | `hero-skin/关羽/2/full.gif` |

---

## 使用方式

### 1. 基础使用（自动查找）

将 GIF 文件按照上述目录结构放置后，系统会自动查找并播放（查找顺序与实码一致，`src/ui/graphicspixmaphoveritem.cpp:267-313`）：

1. **静态图路径推导**：将静态图路径后缀 `.jpg`/`.png` 替换为 `.gif`
   - 预设皮肤：`image/fullskin/generals/full/关羽.jpg` → 优先 `/full/gif/` 子目录，再回退同目录
   - heroskin：`hero-skin/关羽/1/full.png` → `hero-skin/关羽/1/full.gif`（仅同目录）

2. **资源别名（最后检查）**：命中 `animatedgeneral` 别名且文件存在时，覆盖目录搜索结果

### 2. 资源别名（可选）

对于需要特殊处理或跨目录复用的 GIF，可以注册 `animatedgeneral` 资源别名（在 Lua 初始化代码中注册）：

```lua
-- 同目录下的别名文件
sgs.Sanguosha:addResourceAlias("animatedgeneral", "关羽", "关羽_动画版.gif")

-- 支持完整路径
sgs.Sanguosha:addResourceAlias("animatedgeneral", "关羽", "image/special/关羽_animated.gif")
```

注意：

- 查找 key 是静态图文件名去掉尾缀 `_N` 的部分（如 `关羽_1` → `关羽`），heroskin 皮肤不能按编号注册单独别名。
- 别名在目录搜索**之后**才检查；别名文件存在时覆盖目录搜索结果（即别名优先权最高）。

别名查找流程（与实码检查顺序一致）：
```
1. 目录搜索：/full/gif/ 子目录（预设皮肤）→ 同目录 .gif
2. 检查 animatedgeneral 资源别名：命中则覆盖（最高优先权）
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

2. **目录创建**：预设皮肤需在 `image/fullskin/generals/full/` 下创建 `gif/` 子文件夹；heroskin 皮肤直接将 `full.gif` 放在皮肤目录即可，无需 `gif/` 子目录

3. **文件格式**：仅支持 `.gif` 格式，不支持其他动画格式

4. **回退机制**：当 GIF 文件不存在或加载失败时，自动回退到静态图显示

---

## 相关文档

- [动态皮肤功能使用文档](dynamic-skin-guide.md) - Spine 动画系统说明
- [场景切换与语音动画移植说明](場景切換與語音動畫移植說明.md)
