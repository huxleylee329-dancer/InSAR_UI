#pragma once

class QWidget;

namespace QtNodes {

class TSXBatchImportNode;

QWidget* createTsxBatchImportValidationWidget(TSXBatchImportNode* node, QWidget* parent);

} // namespace QtNodes
