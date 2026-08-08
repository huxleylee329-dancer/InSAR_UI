#pragma once

#include <memory>

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVector>

#include "Export.hpp"
#include "ProductContracts.hpp"

namespace QtNodes {

/**
 * `id` represents an internal unique data type for given port.
 * `name` is a normal text description.
 */
struct NODE_EDITOR_PUBLIC NodeDataType
{
    QString id;
    QString name;
};

/**
 * @brief FieldEditType - 字段编辑类型
 */
enum class FieldEditType
{
    None,       // 不可编辑
    Text,       // 文本输入
    Number,     // 数字输入（支持 double）
    Path,       // 文件路径选择
    Boolean     // 布尔开关
};

/**
 * @brief DataField - 数据字段描述
 */
struct NODE_EDITOR_PUBLIC DataField
{
    QString key;
    QString value;
    FieldEditType editType = FieldEditType::None;  // 编辑类型
    double minNumber = -1000000.0;                // 数字最小值
    double maxNumber = 1000000.0;                  // 数字最大值
    int decimals = 2;                              // 小数位数
    QString pathFilter = "All Files (*)";          // 文件过滤器
};

/**
 * Class represents data transferred between nodes.
 * @param type is used for comparing types
 * The actual data is stored in subtypes
 */
class NODE_EDITOR_PUBLIC NodeData
{
public:
    virtual ~NodeData() = default;

    virtual bool sameType(NodeData const &nodeData) const
    {
        return (this->type().id == nodeData.type().id);
    }

    /// Type for inner use
    virtual NodeDataType type() const = 0;

    /// Semantic identity attached by the producing port. The descriptor is
    /// intentionally runtime-only; persisted artifacts keep their own copy.
    ProductDescriptor::Ptr productDescriptor() const { return _productDescriptor; }
    void setProductDescriptor(ProductDescriptor::Ptr descriptor) { _productDescriptor = descriptor; }

    /// Physical identity of the referenced H5 artifact. This differs from the
    /// logical descriptor when a node publishes a zero-copy reference output.
    ProductDescriptor::Ptr physicalProductDescriptor() const
    {
        return _physicalProductDescriptor ? _physicalProductDescriptor : _productDescriptor;
    }
    void setPhysicalProductDescriptor(ProductDescriptor::Ptr descriptor)
    {
        _physicalProductDescriptor = descriptor;
    }

    /**
     * @brief getSummary - 获取数据摘要（单行简短描述）
     */
    virtual QString getSummary() const {
        return type().name;
    }

    /**
     * @brief getFields - 获取数据字段详情
     */
    virtual QVector<DataField> getFields() const {
        return {};
    }

    /**
     * @brief setField - 设置字段值
     * @param key 字段名称
     * @param value 新值
     * @return 是否设置成功
     */
    virtual bool setField(const QString& key, const QString& value) {
        Q_UNUSED(key);
        Q_UNUSED(value);
        return false;
    }

private:
    ProductDescriptor::Ptr _productDescriptor;
    ProductDescriptor::Ptr _physicalProductDescriptor;
};

} // namespace QtNodes
Q_DECLARE_METATYPE(QtNodes::NodeDataType)
Q_DECLARE_METATYPE(std::shared_ptr<QtNodes::NodeData>)
