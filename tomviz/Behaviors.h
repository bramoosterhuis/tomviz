/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizBehaviors_h
#define tomvizBehaviors_h

#include <QObject>

class QMainWindow;
class vtkSMTransferFunctionPresets;

namespace tomviz {

class MoveActiveObject;

/// Behaviors instantiates tomviz relevant ParaView behaviors (and any new
/// ones) as needed.

class Behaviors : public QObject
{
  Q_OBJECT

public:
  Behaviors(QMainWindow* mainWindow);

  MoveActiveObject* moveActiveBehavior() { return m_moveActiveBehavior; }

private:
  Q_DISABLE_COPY(Behaviors)

  MoveActiveObject* m_moveActiveBehavior;

  QString getMatplotlibColorMapFile();

  // Caller is responsible for calling Delete() on the returned object.
  vtkSMTransferFunctionPresets* getPresets();

  /// Use the named color map preset as default.
  void setDefaultColorMapFromPreset(const char* name);

  void registerCustomOperatorUIs();
};
} // namespace tomviz
#endif
