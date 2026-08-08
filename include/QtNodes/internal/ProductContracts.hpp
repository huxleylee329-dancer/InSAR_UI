#pragma once

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QMap>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <memory>

namespace QtNodes {

enum class ProductState
{
    Unknown,
    Identified,
    Staged,
    Committed,
    Invalid
};

// Product descriptors describe an artifact, not the node instance that produced it.
// They are immutable so a published result cannot be reclassified in place.
class ProductDescriptor
{
public:
    using Ptr = std::shared_ptr<const ProductDescriptor>;

    static Ptr create(const QString& productType,
                      const QString& schemaId,
                      int schemaVersion,
                      ProductState state,
                      const QString& source,
                      const QMap<QString, QString>& provenance = QMap<QString, QString>())
    {
        return Ptr(new ProductDescriptor(productType, schemaId, schemaVersion, state,
                                         source, provenance));
    }

    static Ptr fromJson(const QJsonObject& object, QString* errorMessage = nullptr)
    {
        const QString productType = object.value(QStringLiteral("product_type")).toString();
        const QString schemaId = object.value(QStringLiteral("schema_id")).toString();
        const int schemaVersion = object.value(QStringLiteral("schema_version")).toInt(-1);
        const int stateValue = object.value(QStringLiteral("state")).toInt(-1);
        const QString source = object.value(QStringLiteral("source")).toString();
        if (productType.isEmpty() || schemaId.isEmpty() || schemaVersion < 1 ||
            stateValue < static_cast<int>(ProductState::Unknown) ||
            stateValue > static_cast<int>(ProductState::Invalid) || source.isEmpty()) {
            if (errorMessage) *errorMessage = QStringLiteral("Product descriptor is incomplete.");
            return Ptr();
        }

        QMap<QString, QString> provenance;
        const QJsonObject provenanceObject = object.value(QStringLiteral("provenance")).toObject();
        for (auto it = provenanceObject.constBegin(); it != provenanceObject.constEnd(); ++it) {
            if (!it.value().isString()) {
                if (errorMessage) *errorMessage = QStringLiteral("Product descriptor provenance is invalid.");
                return Ptr();
            }
            provenance.insert(it.key(), it.value().toString());
        }
        return create(productType, schemaId, schemaVersion,
                      static_cast<ProductState>(stateValue), source, provenance);
    }

    QString productType() const { return _productType; }
    QString schemaId() const { return _schemaId; }
    int schemaVersion() const { return _schemaVersion; }
    ProductState state() const { return _state; }
    QString source() const { return _source; }
    QMap<QString, QString> provenance() const { return _provenance; }

    QJsonObject toJson() const
    {
        QJsonObject object;
        object.insert(QStringLiteral("product_type"), _productType);
        object.insert(QStringLiteral("schema_id"), _schemaId);
        object.insert(QStringLiteral("schema_version"), _schemaVersion);
        object.insert(QStringLiteral("state"), static_cast<int>(_state));
        object.insert(QStringLiteral("source"), _source);
        QJsonObject provenanceObject;
        for (auto it = _provenance.constBegin(); it != _provenance.constEnd(); ++it) {
            provenanceObject.insert(it.key(), it.value());
        }
        object.insert(QStringLiteral("provenance"), provenanceObject);
        return object;
    }

private:
    ProductDescriptor(const QString& productType,
                      const QString& schemaId,
                      int schemaVersion,
                      ProductState state,
                      const QString& source,
                      const QMap<QString, QString>& provenance)
        : _productType(productType)
        , _schemaId(schemaId)
        , _schemaVersion(schemaVersion)
        , _state(state)
        , _source(source)
        , _provenance(provenance)
    {
    }

    const QString _productType;
    const QString _schemaId;
    const int _schemaVersion;
    const ProductState _state;
    const QString _source;
    const QMap<QString, QString> _provenance;
};

struct ProductInputContract
{
    QString semanticId;
    bool optional = false;
    QStringList allowedProductTypes;
    QString schemaId = QStringLiteral("sat-explorer-product");
    int minimumSchemaVersion = 1;
    int maximumSchemaVersion = 1;
    QList<ProductState> acceptedStates = {ProductState::Identified, ProductState::Committed};
    QStringList requiredProvenanceFields;
};

struct ProductOutputContract
{
    QString semanticId;
    QStringList publishedProductTypes;
    QString schemaId = QStringLiteral("sat-explorer-product");
    int schemaVersion = 1;
    ProductState publishedState = ProductState::Committed;
};

struct ProductValidationResult
{
    bool accepted = false;
    QString reason;
};

inline ProductValidationResult validateConnection(const ProductOutputContract& source,
                                                   const ProductInputContract& destination)
{
    if (source.semanticId.isEmpty() || destination.semanticId.isEmpty() ||
        source.publishedProductTypes.isEmpty() || destination.allowedProductTypes.isEmpty()) {
        return {false, QStringLiteral("Port contract is incomplete.")};
    }

    // A multi-product output is connectable only when every product it can publish
    // satisfies the input contract. An intersection is intentionally insufficient.
    for (const QString& productType : source.publishedProductTypes) {
        if (!destination.allowedProductTypes.contains(productType)) {
            return {false, QStringLiteral("Output product '%1' is not accepted by input '%2'.")
                               .arg(productType, destination.semanticId)};
        }
    }
    if (source.schemaId != destination.schemaId ||
        source.schemaVersion < destination.minimumSchemaVersion ||
        source.schemaVersion > destination.maximumSchemaVersion) {
        return {false, QStringLiteral("Descriptor schema is not accepted by input '%1'.")
                           .arg(destination.semanticId)};
    }
    return {true, QString()};
}

inline ProductValidationResult validateBoundDescriptor(const ProductInputContract& contract,
                                                        const ProductDescriptor::Ptr& descriptor)
{
    if (!descriptor) {
        return {false, QStringLiteral("No identified product descriptor is bound to this input.")};
    }
    if (!contract.allowedProductTypes.contains(descriptor->productType())) {
        return {false, QStringLiteral("Bound product '%1' is not accepted by input '%2'.")
                           .arg(descriptor->productType(), contract.semanticId)};
    }
    if (descriptor->schemaId() != contract.schemaId ||
        descriptor->schemaVersion() < contract.minimumSchemaVersion ||
        descriptor->schemaVersion() > contract.maximumSchemaVersion) {
        return {false, QStringLiteral("Bound descriptor schema is not accepted by input '%1'.")
                           .arg(contract.semanticId)};
    }
    if (!contract.acceptedStates.contains(descriptor->state())) {
        return {false, QStringLiteral("Bound product state is not executable for input '%1'.")
                           .arg(contract.semanticId)};
    }
    const QMap<QString, QString> provenance = descriptor->provenance();
    for (const QString& field : contract.requiredProvenanceFields) {
        if (!provenance.contains(field) || provenance.value(field).isEmpty()) {
            return {false, QStringLiteral("Bound descriptor is missing provenance field '%1'.").arg(field)};
        }
    }
    return {true, QString()};
}

inline ProductValidationResult validatePublishedDescriptor(const ProductOutputContract& contract,
                                                           const ProductDescriptor::Ptr& descriptor)
{
    if (!descriptor) {
        return {false, QStringLiteral("Output has no product descriptor.")};
    }
    if (contract.semanticId.isEmpty() || contract.publishedProductTypes.isEmpty() ||
        !contract.publishedProductTypes.contains(descriptor->productType())) {
        return {false, QStringLiteral("Output descriptor product is not declared by this port.")};
    }
    if (descriptor->schemaId() != contract.schemaId ||
        descriptor->schemaVersion() != contract.schemaVersion ||
        descriptor->state() != contract.publishedState) {
        return {false, QStringLiteral("Output descriptor schema or state is not declared by this port.")};
    }
    const QMap<QString, QString> provenance = descriptor->provenance();
    if (provenance.value(QStringLiteral("producer")).isEmpty() ||
        provenance.value(QStringLiteral("output_port")) != contract.semanticId) {
        return {false, QStringLiteral("Output descriptor provenance does not identify this port.")};
    }
    return {true, QString()};
}

} // namespace QtNodes
