#include "boot-cosmos-item.h"

#include <QElapsedTimer>
#include <QSGGeometryNode>
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <cstring>

namespace {

class BootCosmosMaterial : public QSGMaterial
{
public:
    BootCosmosMaterial() { setFlag(Blending); }

    QSGMaterialType *type() const override
    {
        static QSGMaterialType type;
        return &type;
    }
    QSGMaterialShader *createShader(QSGRendererInterface::RenderMode) const override;
    int compare(const QSGMaterial *other) const override
    {
        return this == other ? 0 : (this < other ? -1 : 1);
    }

    float time = 0;
    QSizeF resolution;
    float cornerRadius = 0;
};

class BootCosmosShader : public QSGMaterialShader
{
public:
    BootCosmosShader()
    {
        setShaderFileName(VertexStage, QStringLiteral(":/QSanguosha/Home/shaders/boot-cosmos.vert.qsb"));
        setShaderFileName(FragmentStage, QStringLiteral(":/QSanguosha/Home/shaders/boot-cosmos.frag.qsb"));
    }

    // The uniform block of boot-cosmos.vert and boot-cosmos.frag.
    bool updateUniformData(RenderState &state, QSGMaterial *newMaterial, QSGMaterial *) override
    {
        const auto *material = static_cast<BootCosmosMaterial *>(newMaterial);
        char *data = state.uniformData()->data();
        const float values[] = {
            state.opacity(), material->time,
            float(material->resolution.width()), float(material->resolution.height()),
            material->cornerRadius
        };
        std::memcpy(data, state.combinedMatrix().constData(), 64);
        std::memcpy(data + 64, values, sizeof(values));
        return true;
    }
};

QSGMaterialShader *BootCosmosMaterial::createShader(QSGRendererInterface::RenderMode) const
{
    return new BootCosmosShader;
}

class BootCosmosNode : public QSGGeometryNode
{
public:
    BootCosmosNode()
        : m_geometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), 4)
    {
        setFlag(UsePreprocess);
        setGeometry(&m_geometry);
        setMaterial(&m_material);
    }

    void setArea(const QRectF &area, qreal cornerRadius)
    {
        QSGGeometry::updateTexturedRectGeometry(&m_geometry, area, QRectF(0, 0, 1, 1));
        m_material.resolution = area.size();
        m_material.cornerRadius = float(cornerRadius);
        markDirty(DirtyGeometry | DirtyMaterial);
    }

    // Runs on the render thread for every frame drawn; the scene starts at the first.
    void preprocess() override
    {
        if (!m_clock.isValid())
            m_clock.start();
        m_material.time = float(m_clock.nsecsElapsed() / 1e9);
        markDirty(DirtyMaterial);
    }

private:
    QSGGeometry m_geometry;
    BootCosmosMaterial m_material;
    QElapsedTimer m_clock;
};

}

BootCosmosItem::BootCosmosItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents);
}

qreal BootCosmosItem::cornerRadius() const
{
    return m_cornerRadius;
}

void BootCosmosItem::setCornerRadius(qreal radius)
{
    if (qFuzzyCompare(m_cornerRadius, radius))
        return;
    m_cornerRadius = radius;
    update();
    emit cornerRadiusChanged();
}

void BootCosmosItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    update();
}

QSGNode *BootCosmosItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    auto *node = static_cast<BootCosmosNode *>(oldNode);
    if (!node)
        node = new BootCosmosNode;
    node->setArea(boundingRect(), m_cornerRadius);
    return node;
}
