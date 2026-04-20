#pragma once

#include "ExecutableNodeDelegateModel.hpp"
#include "Export.hpp"

namespace QtNodes {

class NODE_EDITOR_PUBLIC CardExecutableNodeDelegateModel
    : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    CardExecutableNodeDelegateModel();

    ~CardExecutableNodeDelegateModel() override = default;

public:
    /// Mark that this node uses card layout (not external ears layout)
    bool useExternalLayout() const override { return false; }
};

} // namespace QtNodes
